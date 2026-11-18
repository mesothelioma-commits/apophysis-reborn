#include "vulkan_runtime.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>

namespace apo {
namespace {
// TLS only points at ordinary stack storage. Windows runs TLS destructors in
// its loader callback, where driver shutdown and FreeLibrary can deadlock.
thread_local std::shared_ptr<void> *current_cache = nullptr;
}
gpu::ContextScope::ContextScope() noexcept {
    if (!current_cache)
        current_cache = &context_;
}
gpu::ContextScope::~ContextScope() {
    if (current_cache == &context_)
        current_cache = nullptr;
}
} // namespace apo

namespace apo::gpu {
namespace {
std::uint64_t validate_hit_range(const RenderOptions &options, std::uint64_t first,
                                 std::uint32_t count) {
    if (options.stop_token.stop_requested())
        throw RenderCancelled();
    constexpr std::uint64_t max_samples = UINT64_MAX / ((1u << 24) - 1u);
    if (!options.samples || options.samples > max_samples)
        throw std::invalid_argument("Sample count exceeds the supported accumulator range");
    const auto chains = 1 + (options.samples - 1) / samples_per_chain;
    if (!count || count > 1024 || first >= chains || count > chains - first)
        throw std::invalid_argument("Vulkan hit range must select 1–1024 existing sample chains");
    return chains;
}
} // namespace
} // namespace apo::gpu

#if APO_ENABLE_VULKAN
#define VK_NO_PROTOTYPES
#include "generated/orbit_spv.hpp"
#include <vulkan/vulkan_core.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace apo {
namespace {
using gpu::Unavailable;

std::string result_name(VkResult result) {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "out of host memory";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "out of device memory";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "initialization failed";
    case VK_ERROR_DEVICE_LOST:
        return "device lost";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "incompatible driver";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "required feature unavailable";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "requested validation layer unavailable";
    default:
        return "Vulkan result " + std::to_string(int(result));
    }
}
void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw Unavailable(std::string(operation) + ": " + result_name(result));
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw RenderCancelled();
}
std::uint64_t mib(std::uint64_t value) {
    if (!value || value > UINT64_MAX / (1024 * 1024))
        throw std::invalid_argument("GPU and host memory budgets must be positive and representable");
    return value * 1024 * 1024;
}
std::string version(std::uint32_t value) {
    return std::to_string(VK_VERSION_MAJOR(value)) + "." + std::to_string(VK_VERSION_MINOR(value)) +
           "." + std::to_string(VK_VERSION_PATCH(value));
}

struct Library {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void *handle = nullptr;
#endif
    PFN_vkGetInstanceProcAddr get = nullptr;
    Library() {
        if (const char *disabled = std::getenv("APO_DISABLE_VULKAN");
            disabled && std::string_view(disabled) == "1")
            throw Unavailable("Vulkan disabled by APO_DISABLE_VULKAN=1");
#ifdef _WIN32
        // The graphics driver installs the system loader. An arbitrary DLL in
        // the current working directory is never searched by this backend.
        handle = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (handle)
            get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                GetProcAddress(handle, "vkGetInstanceProcAddr"));
#else
        handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (handle)
            get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
#endif
        if (!handle)
            throw Unavailable(
                "Vulkan loader is unavailable; install a graphics driver with Vulkan support");
        if (!get) {
            close();
            throw Unavailable("Vulkan loader does not export vkGetInstanceProcAddr");
        }
    }
    void close() noexcept {
#ifdef _WIN32
        if (handle)
            FreeLibrary(handle);
#else
        if (handle)
            dlclose(handle);
#endif
        handle = nullptr;
    }
    ~Library() {
        close();
    }
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
};

struct Physical {
    VkPhysicalDevice handle{};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t queue_family = UINT32_MAX;
    ComputeDevice info;
};

struct Instance {
    Library library;
    VkInstance instance{};
    std::uint32_t api = VK_API_VERSION_1_0;
    PFN_vkDestroyInstance DestroyInstance{};
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices{};
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties{};
    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2{};
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures{};
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties{};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties{};
    PFN_vkCreateDevice CreateDevice{};
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr{};
    std::vector<Physical> devices;

    template <class T> void load(T &target, const char *name) {
        target = reinterpret_cast<T>(library.get(instance, name));
        if (!target)
            throw Unavailable(std::string("Vulkan entry point missing: ") + name);
    }
    Instance() {
        auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            library.get({}, "vkEnumerateInstanceVersion"));
        std::uint32_t available = VK_API_VERSION_1_0;
        if (enumerateVersion && enumerateVersion(&available) == VK_SUCCESS &&
            available >= VK_API_VERSION_1_1)
            api = VK_API_VERSION_1_1;
        auto create = reinterpret_cast<PFN_vkCreateInstance>(library.get({}, "vkCreateInstance"));
        if (!create)
            throw Unavailable("Vulkan loader cannot create an instance");
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "Apophysis Reborn";
        application.applicationVersion = VK_MAKE_VERSION(0, 7, 0);
        application.pEngineName = "Reborn compute";
        application.engineVersion = VK_MAKE_VERSION(0, 7, 0);
        application.apiVersion = api;
        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &application;
        check(create(&createInfo, nullptr, &instance), "Create Vulkan instance");
        try {
#define APO_INSTANCE(name) load(name, "vk" #name)
            APO_INSTANCE(DestroyInstance);
            APO_INSTANCE(EnumeratePhysicalDevices);
            APO_INSTANCE(GetPhysicalDeviceProperties);
            APO_INSTANCE(GetPhysicalDeviceFeatures);
            APO_INSTANCE(GetPhysicalDeviceMemoryProperties);
            APO_INSTANCE(GetPhysicalDeviceQueueFamilyProperties);
            APO_INSTANCE(CreateDevice);
            APO_INSTANCE(GetDeviceProcAddr);
            if (api >= VK_API_VERSION_1_1)
                APO_INSTANCE(GetPhysicalDeviceProperties2);
#undef APO_INSTANCE
            enumerate();
        } catch (...) {
            if (DestroyInstance)
                DestroyInstance(instance, nullptr);
            instance = {};
            throw;
        }
    }
    ~Instance() {
        if (instance && DestroyInstance)
            DestroyInstance(instance, nullptr);
    }

    void enumerate() {
        std::vector<VkPhysicalDevice> handles;
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            std::uint32_t count = 0;
            check(EnumeratePhysicalDevices(instance, &count, nullptr), "Count Vulkan devices");
            handles.resize(count);
            const auto result = EnumeratePhysicalDevices(instance, &count, handles.data());
            if (result == VK_INCOMPLETE)
                continue;
            check(result, "Enumerate Vulkan devices");
            handles.resize(count);
            break;
        }
        for (const auto handle : handles) {
            Physical device;
            device.handle = handle;
            GetPhysicalDeviceProperties(handle, &device.properties);
            GetPhysicalDeviceFeatures(handle, &device.features);
            GetPhysicalDeviceMemoryProperties(handle, &device.memory);
            std::uint32_t count = 0;
            GetPhysicalDeviceQueueFamilyProperties(handle, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);
            GetPhysicalDeviceQueueFamilyProperties(handle, &count, queues.data());
            for (std::uint32_t q = 0; q < count; ++q) {
                if (queues[q].queueCount && (queues[q].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                    device.queue_family = q;
                    if (!(queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                        break;
                }
            }
            auto &info = device.info;
            const auto &properties = device.properties;
            info.index = static_cast<unsigned>(devices.size());
            info.name = properties.deviceName;
            info.api_version = version(properties.apiVersion);
            info.driver_version = std::to_string(properties.driverVersion); // Vendor-specific encoding.
            info.float64 = device.features.shaderFloat64;
            info.int64 = device.features.shaderInt64;
            info.software = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
            switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                info.type = "Discrete GPU";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                info.type = "Integrated GPU";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                info.type = "Virtual GPU";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                info.type = "CPU / software Vulkan";
                break;
            default:
                info.type = "Other Vulkan device";
                break;
            }
            const std::uint8_t *identifier = properties.pipelineCacheUUID;
            VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            if (GetPhysicalDeviceProperties2 && properties.apiVersion >= VK_API_VERSION_1_1) {
                VkPhysicalDeviceProperties2 extended{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                extended.pNext = &ids;
                GetPhysicalDeviceProperties2(handle, &extended);
                if (std::any_of(std::begin(ids.deviceUUID), std::end(ids.deviceUUID),
                                [](auto b) { return b != 0; }))
                    identifier = ids.deviceUUID;
            }
            std::ostringstream key;
            key << "vk:" << std::hex << properties.vendorID << ':' << properties.deviceID << ':';
            for (unsigned i = 0; i < VK_UUID_SIZE; ++i)
                key << std::setw(2) << std::setfill('0') << unsigned(identifier[i]);
            if (identifier == properties.pipelineCacheUUID)
                key << ':' << info.index;
            info.id = key.str();
            for (std::uint32_t h = 0; h < device.memory.memoryHeapCount; ++h)
                if (device.memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    info.device_memory_mib += device.memory.memoryHeaps[h].size / (1024 * 1024);
            info.max_storage_buffer_bytes = properties.limits.maxStorageBufferRange;
            const auto &limits = properties.limits;
            if (device.queue_family == UINT32_MAX)
                info.reason = "No compute queue";
            else if (limits.maxComputeWorkGroupInvocations < gpu::workgroup_size ||
                     limits.maxComputeWorkGroupSize[0] < gpu::workgroup_size)
                info.reason = "Compute workgroup limit is below 64";
            else if (limits.maxPerStageDescriptorStorageBuffers < 4 ||
                     limits.maxDescriptorSetStorageBuffers < 4)
                info.reason = "Fewer than four storage buffer bindings";
            else if (limits.maxPushConstantsSize < sizeof(gpu::PushConstants))
                info.reason = "Push constant limit is below 32 bytes";
            else if (!limits.maxComputeWorkGroupCount[0])
                info.reason = "No compute dispatch capacity";
            info.usable = info.reason.empty();
            devices.push_back(std::move(device));
        }
    }
};

const Physical &select(const Instance &instance, const RenderOptions &options) {
    if (!options.device.empty()) {
        unsigned index = 0;
        const auto parsed =
            std::from_chars(options.device.data(), options.device.data() + options.device.size(), index);
        const bool numeric =
            parsed.ec == std::errc{} && parsed.ptr == options.device.data() + options.device.size();
        for (const auto &device : instance.devices) {
            if ((numeric && device.info.index == index) || device.info.id == options.device) {
                if (!device.info.usable)
                    throw Unavailable(device.info.name + ": " + device.info.reason);
                return device;
            }
        }
        throw Unavailable("Selected Vulkan device is unavailable: " + options.device);
    }
    const Physical *chosen = nullptr;
    auto rank = [](VkPhysicalDeviceType type) {
        switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 5;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 4;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return 3;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return 1;
        default:
            return 2;
        }
    };
    for (const auto &device : instance.devices) {
        if (!device.info.usable || (options.backend == RenderBackend::automatic && device.info.software))
            continue;
        if (!chosen || rank(device.properties.deviceType) > rank(chosen->properties.deviceType))
            chosen = &device;
    }
    if (!chosen)
        throw Unavailable(
            options.backend == RenderBackend::automatic
                ? "No suitable hardware Vulkan GPU; automatic selection excludes software devices"
                : "No suitable Vulkan compute device");
    return *chosen;
}

struct Context {
    std::shared_ptr<Instance> instance;
    Physical physical;
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool command_pool{};
    VkCommandBuffer command{};
    VkDescriptorSetLayout descriptor_layout{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkFence fence{};
#define APO_DEVICE_LIST(X)                                                                              \
    X(DestroyDevice)                                                                                    \
    X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
        X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory)                 \
            X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) X(CreateShaderModule)            \
                X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout)       \
                    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines)          \
                        X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool)             \
                            X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateCommandPool)      \
                                X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool)     \
                                    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdBindPipeline)        \
                                        X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)     \
                                            X(CmdPipelineBarrier) X(CmdFillBuffer) X(CmdCopyBuffer)     \
                                                X(CreateFence) X(DestroyFence) X(ResetFences)           \
                                                    X(WaitForFences) X(QueueSubmit)
#define APO_MEMBER(name) PFN_vk##name name{};
    APO_DEVICE_LIST(APO_MEMBER)
#undef APO_MEMBER
    template <class T> void load(T &target, const char *name) {
        target = reinterpret_cast<T>(instance->GetDeviceProcAddr(device, name));
        if (!target)
            throw Unavailable(std::string("Vulkan device entry point missing: ") + name);
    }
    Context(std::shared_ptr<Instance> owner, const Physical &chosen)
        : instance(std::move(owner)), physical(chosen) {
        const float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = physical.queue_family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{};
        features.robustBufferAccess = physical.features.robustBufferAccess;
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.pEnabledFeatures = &features;
        check(instance->CreateDevice(physical.handle, &createInfo, nullptr, &device),
              "Create compute device");
        try {
#define APO_LOAD(name) load(name, "vk" #name);
            APO_DEVICE_LIST(APO_LOAD)
#undef APO_LOAD
            GetDeviceQueue(device, physical.queue_family, 0, &queue);
            create_pipeline();
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.queueFamilyIndex = physical.queue_family;
            pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            check(CreateCommandPool(device, &pool, nullptr, &command_pool), "Create command pool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocation.commandPool = command_pool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            check(AllocateCommandBuffers(device, &allocation, &command), "Allocate command buffer");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(CreateFence(device, &fenceInfo, nullptr, &fence), "Create compute fence");
        } catch (...) {
            close();
            throw;
        }
    }
    void create_pipeline() {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (unsigned i = 0; i < bindings.size(); ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptors.bindingCount = static_cast<std::uint32_t>(bindings.size());
        descriptors.pBindings = bindings.data();
        check(CreateDescriptorSetLayout(device, &descriptors, nullptr, &descriptor_layout),
              "Create compute bindings");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gpu::PushConstants)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &descriptor_layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        check(CreatePipelineLayout(device, &layout, nullptr, &pipeline_layout),
              "Create compute pipeline layout");
        VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = sizeof(gpu::orbit_spv);
        shaderInfo.pCode = gpu::orbit_spv;
        VkShaderModule shader{};
        check(CreateShaderModule(device, &shaderInfo, nullptr, &shader), "Create orbit shader");
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module = shader;
        compute.stage.pName = "main";
        compute.layout = pipeline_layout;
        const auto result = CreateComputePipelines(device, {}, 1, &compute, nullptr, &pipeline);
        DestroyShaderModule(device, shader, nullptr);
        check(result, "Compile compute pipeline");
    }
    void close() noexcept {
        if (!device)
            return;
        if (DeviceWaitIdle)
            DeviceWaitIdle(device);
        if (fence && DestroyFence)
            DestroyFence(device, fence, nullptr);
        if (command_pool && DestroyCommandPool)
            DestroyCommandPool(device, command_pool, nullptr);
        if (pipeline && DestroyPipeline)
            DestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout && DestroyPipelineLayout)
            DestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_layout && DestroyDescriptorSetLayout)
            DestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (DestroyDevice)
            DestroyDevice(device, nullptr);
        device = {};
    }
    ~Context() {
        close();
    }
    void begin() {
        check(ResetCommandPool(device, command_pool, 0), "Reset compute commands");
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(BeginCommandBuffer(command, &beginInfo), "Begin compute commands");
    }
    void submit(std::stop_token stop) {
        check(EndCommandBuffer(command), "End compute commands");
        check(ResetFences(device, 1, &fence), "Reset compute fence");
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command;
        check(QueueSubmit(queue, 1, &submitInfo, fence), "Submit compute work");
        // Cancellation waits for this bounded dispatch before buffers can be
        // destroyed. No queued device work ever outlives its resource owner.
        for (;;) {
            const auto result = WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000));
            if (result == VK_TIMEOUT)
                continue;
            check(result, "Wait for compute work");
            break;
        }
        cancelled(stop);
    }
};
#undef APO_DEVICE_LIST

void discard_context() {
    if (current_cache)
        current_cache->reset();
}
std::shared_ptr<Context> context(const RenderOptions &options) {
    if (const char *disabled = std::getenv("APO_DISABLE_VULKAN");
        disabled && std::string_view(disabled) == "1")
        throw Unavailable("Vulkan disabled by APO_DISABLE_VULKAN=1");
    if (current_cache && *current_cache) {
        auto cached = std::static_pointer_cast<Context>(*current_cache);
        const auto &chosen = select(*cached->instance, options);
        if (chosen.info.id == cached->physical.info.id)
            return cached;
    }
    auto instance = std::make_shared<Instance>();
    const auto &chosen = select(*instance, options);
    auto created = std::make_shared<Context>(instance, chosen);
    if (current_cache)
        *current_cache = created;
    return created;
}

struct Budget {
    std::uint64_t host_limit, device_limit, host_used, device_used = 0;
    void reserve(std::uint64_t bytes, bool host, bool local) {
        if (host && (host_used > host_limit || bytes > host_limit - host_used))
            throw Unavailable("Vulkan readback and host buffers exceed --memory-mib");
        if (local && (device_used > device_limit || bytes > device_limit - device_used))
            throw Unavailable("Vulkan buffers exceed --gpu-memory-mib");
        if (host)
            host_used += bytes;
        if (local)
            device_used += bytes;
    }
};

struct Buffer {
    Context &context;
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceSize size = 0;
    void *mapped = nullptr;
    bool coherent = false;
    Buffer(Context &owner, VkDeviceSize bytes, VkBufferUsageFlags usage, bool host, Budget &budget)
        : context(owner), size(std::max<VkDeviceSize>(bytes, 16)) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(context.CreateBuffer(context.device, &info, nullptr, &buffer), "Create compute buffer");
        try {
            VkMemoryRequirements requirements{};
            context.GetBufferMemoryRequirements(context.device, buffer, &requirements);
            const auto &properties = context.physical.memory;
            unsigned chosen = UINT32_MAX;
            int best = -1;
            for (unsigned i = 0; i < properties.memoryTypeCount; ++i) {
                if (!(requirements.memoryTypeBits & (1u << i)))
                    continue;
                const auto flags = properties.memoryTypes[i].propertyFlags;
                if (host && !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                    continue;
                const int score = host ? ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 2 : 0) +
                                             ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 1 : 0)
                                       : ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 4 : 0);
                if (score > best) {
                    best = score;
                    chosen = i;
                }
            }
            if (chosen == UINT32_MAX)
                throw Unavailable("No suitable Vulkan buffer memory type");
            const auto flags = properties.memoryTypes[chosen].propertyFlags;
            const auto heap = properties.memoryTypes[chosen].heapIndex;
            if (requirements.size > properties.memoryHeaps[heap].size)
                throw Unavailable("Compute allocation exceeds the selected device memory heap");
            // Device-facing buffers always count toward the GPU budget, including
            // memory types that a driver implements outside a DEVICE_LOCAL heap.
            const bool local = !host || (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            budget.reserve(requirements.size, host, local);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = chosen;
            check(context.AllocateMemory(context.device, &allocation, nullptr, &memory),
                  "Allocate compute memory");
            check(context.BindBufferMemory(context.device, buffer, memory, 0), "Bind compute memory");
            coherent = flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if (host)
                check(context.MapMemory(context.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
                      "Map compute memory");
        } catch (...) {
            close();
            throw;
        }
    }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    void close() noexcept {
        if (mapped)
            context.UnmapMemory(context.device, memory);
        if (buffer)
            context.DestroyBuffer(context.device, buffer, nullptr);
        if (memory)
            context.FreeMemory(context.device, memory, nullptr);
        mapped = nullptr;
        buffer = {};
        memory = {};
    }
    ~Buffer() {
        close();
    }
    void flush() {
        if (coherent)
            return;
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        check(context.FlushMappedMemoryRanges(context.device, 1, &range), "Flush compute input");
    }
    void invalidate() {
        if (coherent)
            return;
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        check(context.InvalidateMappedMemoryRanges(context.device, 1, &range), "Read compute output");
    }
};

struct Bindings {
    Context &context;
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    Bindings(Context &owner, std::array<Buffer *, 4> buffers) : context(owner) {
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = 1;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;
        check(context.CreateDescriptorPool(context.device, &info, nullptr, &pool),
              "Create compute descriptor pool");
        try {
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = pool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &context.descriptor_layout;
            check(context.AllocateDescriptorSets(context.device, &allocation, &set),
                  "Allocate compute descriptors");
            std::array<VkDescriptorBufferInfo, 4> bufferInfo{};
            std::array<VkWriteDescriptorSet, 4> writes{};
            for (unsigned i = 0; i < buffers.size(); ++i) {
                bufferInfo[i] = {buffers[i]->buffer, 0, buffers[i]->size};
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = &bufferInfo[i];
            }
            context.UpdateDescriptorSets(context.device, static_cast<std::uint32_t>(writes.size()),
                                         writes.data(), 0, nullptr);
        } catch (...) {
            context.DestroyDescriptorPool(context.device, pool, nullptr);
            pool = {};
            throw;
        }
    }
    ~Bindings() {
        if (pool)
            context.DestroyDescriptorPool(context.device, pool, nullptr);
    }
    void bind(const gpu::PushConstants &push) {
        context.CmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline);
        context.CmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                      context.pipeline_layout, 0, 1, &set, 0, nullptr);
        context.CmdPushConstants(context.command, context.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                 0, sizeof(push), &push);
        context.CmdDispatch(context.command,
                            (push.chain_count + gpu::workgroup_size - 1) / gpu::workgroup_size, 1, 1);
    }
};

void barrier(Context &context, VkPipelineStageFlags source, VkPipelineStageFlags destination,
             VkAccessFlags source_access, VkAccessFlags destination_access) {
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory.srcAccessMask = source_access;
    memory.dstAccessMask = destination_access;
    context.CmdPipelineBarrier(context.command, source, destination, 0, 1, &memory, 0, nullptr, 0,
                               nullptr);
}
void storage_size(const Context &context, std::uint64_t bytes) {
    if (bytes > context.physical.properties.limits.maxStorageBufferRange)
        throw Unavailable(
            "Requested storage buffer exceeds this device's maxStorageBufferRange (" +
            std::to_string(context.physical.properties.limits.maxStorageBufferRange / (1024 * 1024)) +
            " MiB)");
}
std::uint64_t read64(const std::uint32_t *words) {
    return std::uint64_t(words[0]) | (std::uint64_t(words[1]) << 32);
}
std::string hit_identity(const Physical &device) {
    // This identifies sampling compatibility; checkpoint file integrity is
    // checked separately by the file renderer. Hash every SPIR-V byte in its
    // specified little-endian order rather than using host object padding.
    static const std::uint64_t kernel = [] {
        std::uint64_t hash = UINT64_C(14695981039346656037);
        for (auto word : gpu::orbit_spv)
            for (unsigned byte = 0; byte < 4; ++byte)
                hash = (hash ^ ((word >> (byte * 8)) & 255u)) * UINT64_C(1099511628211);
        return hash;
    }();
    std::ostringstream result;
    result << "reborn-0.7.0/vulkan-hit-v1|" << device.info.id << "|api=" << device.info.api_version
           << "|driver=" << device.info.driver_version << "|pipeline=" << std::hex << std::setfill('0');
    for (auto byte : device.properties.pipelineCacheUUID)
        result << std::setw(2) << unsigned(byte);
    result << "|spirv-fnv1a64=" << std::setw(16) << kernel;
    return result.str();
}
} // namespace

ComputeInfo compute_devices() {
    ComputeInfo info;
    info.compiled = true;
    try {
        auto instance = std::make_shared<Instance>();
        info.loader_available = true;
        for (const auto &device : instance->devices)
            info.devices.push_back(device.info);
        if (info.devices.empty())
            info.diagnostic = "Vulkan loader is available but no devices were reported";
    } catch (const std::exception &error) {
        info.diagnostic = error.what();
        // Distinguish a load failure from an installed loader without a working ICD.
        try {
            Library library;
            info.loader_available = true;
        } catch (...) {
        }
    }
    return info;
}

namespace gpu {
SampledHistogram sample(const Flame &flame, const RenderOptions &options, const Geometry &geometry,
                        std::uint64_t host_reserved_bytes) {
    cancelled(options.stop_token);
    const auto reason = support_reason(flame);
    if (!reason.empty())
        throw Unavailable(reason);
    PackedFlame packed;
    try {
        packed = pack_flame(flame, geometry);
    } catch (const std::invalid_argument &error) {
        throw Unavailable(error.what());
    }
    try {
        auto owner = context(options);
        auto &ctx = *owner;
        const auto count = std::uint64_t(geometry.width) * geometry.height;
        if (!count || count > UINT32_MAX / 8u)
            throw Unavailable("GPU histogram address range exceeded");
        const auto histogram_bytes = count * sizeof(std::array<std::uint64_t, 4>);
        storage_size(ctx, histogram_bytes);
        storage_size(ctx, packed.words.size() * 4);
        if (host_reserved_bytes > UINT64_MAX - packed.words.size() * 4)
            throw Unavailable("GPU host storage size overflow");
        Budget budget{mib(options.memory_mib), mib(options.gpu_memory_mib),
                      host_reserved_bytes + packed.words.size() * 4};
        Buffer model(ctx, packed.words.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer histogram(ctx, histogram_bytes,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         false, budget);
        Buffer counters(ctx, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        true, budget);
        Buffer probes(ctx, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer readback(ctx, histogram_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, budget);
        std::memcpy(model.mapped, packed.words.data(), packed.words.size() * 4);
        model.flush();
        Bindings bindings(ctx, {&model, &histogram, &counters, &probes});
        const auto chains = (options.samples + samples_per_chain - 1) / samples_per_chain;
        // At most 262,144 samples per submission. A stop request is observed
        // between submissions; the OS never sees one unbounded sample dispatch.
        const auto capacity = std::min<std::uint64_t>(
            1024,
            std::uint64_t(ctx.physical.properties.limits.maxComputeWorkGroupCount[0]) * workgroup_size);
        if (options.progress)
            options.progress(0, options.samples);
        for (std::uint64_t base = 0; base < chains;) {
            cancelled(options.stop_token);
            const auto n = static_cast<std::uint32_t>(std::min(chains - base, capacity));
            const bool last = base + n == chains;
            PushConstants push;
            push.chain_base_low = std::uint32_t(base);
            push.chain_base_high = std::uint32_t(base >> 32);
            push.chain_count = n;
            push.last_chain_samples =
                last ? std::uint32_t(options.samples - (chains - 1) * samples_per_chain)
                     : samples_per_chain;
            push.seed_low = std::uint32_t(options.seed);
            push.seed_high = std::uint32_t(options.seed >> 32);
            ctx.begin();
            if (!base) {
                ctx.CmdFillBuffer(ctx.command, histogram.buffer, 0, VK_WHOLE_SIZE, 0);
                ctx.CmdFillBuffer(ctx.command, counters.buffer, 0, VK_WHOLE_SIZE, 0);
                barrier(ctx, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            } else {
                barrier(ctx, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            }
            bindings.bind(push);
            if (last) {
                barrier(ctx, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT);
                VkBufferCopy copy{0, 0, histogram_bytes};
                ctx.CmdCopyBuffer(ctx.command, histogram.buffer, readback.buffer, 1, &copy);
                barrier(ctx, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
            }
            ctx.submit(options.stop_token);
            base += n;
            if (options.progress)
                options.progress(std::min(options.samples, base * samples_per_chain), options.samples);
        }
        readback.invalidate();
        counters.invalidate();
        SampledHistogram result;
        result.device_name =
            ctx.physical.info.name + (ctx.physical.info.software ? " (software Vulkan)" : "");
        const auto *totals = static_cast<const std::uint32_t *>(counters.mapped);
        result.plotted = read64(totals);
        result.invalid = read64(totals + 2);
        result.bins.resize(static_cast<std::size_t>(count));
        const auto *words = static_cast<const std::uint32_t *>(readback.mapped);
        for (std::size_t i = 0; i < result.bins.size(); ++i) {
            if ((i & 16383u) == 0)
                cancelled(options.stop_token);
            for (unsigned c = 0; c < 4; ++c)
                result.bins[i][c] = read64(words + i * 8 + c * 2);
        }
        return result;
    } catch (const Unavailable &) {
        discard_context();
        throw;
    }
}

SampledHits sample_hits(const Flame &flame, const RenderOptions &options, const Geometry &geometry,
                        std::uint64_t first_chain, std::uint32_t chain_count,
                        std::uint64_t host_reserved_bytes) {
    const auto chains = validate_hit_range(options, first_chain, chain_count);
    if (const auto reason = support_reason(flame); !reason.empty())
        throw Unavailable(reason);
    PackedFlame packed;
    try {
        packed = pack_flame(flame, geometry, true);
    } catch (const std::invalid_argument &error) {
        throw Unavailable(error.what());
    }
    try {
        auto owner = context(options);
        auto &ctx = *owner;
        if (chain_count >
            std::uint64_t(ctx.physical.properties.limits.maxComputeWorkGroupCount[0]) * workgroup_size)
            throw Unavailable("Vulkan hit range exceeds the device dispatch limit");
        using Hit = std::array<std::uint32_t, 6>;
        static_assert(sizeof(Hit) == 24);
        const auto capacity = std::uint64_t(chain_count) * samples_per_chain;
        const auto hit_bytes = capacity * sizeof(Hit);
        storage_size(ctx, hit_bytes);
        storage_size(ctx, packed.words.size() * sizeof(std::uint32_t));
        Budget budget{mib(options.memory_mib), mib(options.gpu_memory_mib), 0};
        budget.reserve(host_reserved_bytes, true, false);
        budget.reserve(hit_bytes, true, false); // Returned vector capacity.
        budget.reserve(packed.words.capacity() * sizeof(std::uint32_t), true, false);
        Buffer model(ctx, packed.words.size() * sizeof(std::uint32_t),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer hits(ctx, hit_bytes,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    false, budget);
        Buffer counters(ctx, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        true, budget);
        Buffer probes(ctx, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer readback(ctx, hit_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, budget);
        std::memcpy(model.mapped, packed.words.data(), packed.words.size() * sizeof(std::uint32_t));
        model.flush();
        Bindings bindings(ctx, {&model, &hits, &counters, &probes});
        PushConstants push;
        push.mode = 2;
        push.chain_base_low = std::uint32_t(first_chain);
        push.chain_base_high = std::uint32_t(first_chain >> 32);
        push.chain_count = chain_count;
        push.last_chain_samples =
            first_chain + chain_count == chains
                ? std::uint32_t(options.samples - (chains - 1) * samples_per_chain)
                : samples_per_chain;
        push.seed_low = std::uint32_t(options.seed);
        push.seed_high = std::uint32_t(options.seed >> 32);
        cancelled(options.stop_token);
        ctx.begin();
        ctx.CmdFillBuffer(ctx.command, hits.buffer, 0, VK_WHOLE_SIZE, UINT32_MAX);
        ctx.CmdFillBuffer(ctx.command, counters.buffer, 0, VK_WHOLE_SIZE, 0);
        barrier(ctx, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        bindings.bind(push);
        barrier(ctx, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT);
        VkBufferCopy copy{0, 0, hit_bytes};
        ctx.CmdCopyBuffer(ctx.command, hits.buffer, readback.buffer, 1, &copy);
        barrier(ctx, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        ctx.submit(options.stop_token);
        readback.invalidate();
        counters.invalidate();
        SampledHits result;
        result.device_name =
            ctx.physical.info.name + (ctx.physical.info.software ? " (software Vulkan)" : "");
        result.checkpoint_identity = hit_identity(ctx.physical);
        const auto *totals = static_cast<const std::uint32_t *>(counters.mapped);
        result.plotted = read64(totals);
        result.invalid = read64(totals + 2);
        const auto range_samples =
            std::uint64_t(chain_count - 1) * samples_per_chain + push.last_chain_samples;
        if (result.plotted > range_samples || result.invalid > range_samples + chain_count * burn_in)
            throw Unavailable("Vulkan hit stream returned invalid sample counters");
        result.hits.reserve(static_cast<std::size_t>(capacity));
        const auto *words = static_cast<const std::uint32_t *>(readback.mapped);
        for (std::uint64_t slot = 0; slot < capacity; ++slot) {
            if ((slot & 16383u) == 0)
                cancelled(options.stop_token);
            const auto *record = words + slot * 6;
            if (record[0] == UINT32_MAX)
                continue;
            if (record[0] >= geometry.width || record[1] >= geometry.height)
                throw Unavailable("Vulkan hit stream returned an invalid global coordinate");
            result.hits.push_back({record[0], record[1], record[2], record[3], record[4], record[5]});
        }
        if (result.hits.size() != result.plotted)
            throw Unavailable("Vulkan hit stream counter does not match its valid records");
        cancelled(options.stop_token);
        return result;
    } catch (const Unavailable &) {
        discard_context();
        throw;
    }
}

std::vector<std::uint32_t> run_probes(const PackedFlame &flame, std::span<const std::uint32_t> words,
                                      const RenderOptions &options) {
    cancelled(options.stop_token);
    if (words.empty() || words.size() % probe_words)
        throw std::invalid_argument("Probe records must contain 16 words each");
    if (flame.words.size() < header_words || !flame.words[0] || flame.words[0] > max_transforms)
        throw std::invalid_argument("Invalid packed probe model header");
    const auto transform_count = flame.words[0] + (flame.words[1] == flame.words[0] ? 1u : 0u);
    if (flame.words.size() < header_words + transform_count * transform_words)
        throw std::invalid_argument("Packed probe model is truncated");
    for (std::size_t record = 0; record < words.size(); record += probe_words) {
        if (words[record] >= transform_count || !words[record + 12] || words[record + 12] > 1024)
            throw std::invalid_argument(
                "Probe transform index or repetition count is outside the supported range");
    }
    try {
        auto owner = context(options);
        auto &ctx = *owner;
        const auto count = words.size() / probe_words;
        if (count > std::uint64_t(ctx.physical.properties.limits.maxComputeWorkGroupCount[0]) *
                        workgroup_size ||
            count > UINT32_MAX)
            throw Unavailable("Probe count exceeds the device dispatch limit");
        storage_size(ctx, flame.words.size() * 4);
        storage_size(ctx, words.size_bytes());
        Budget budget{mib(options.memory_mib), mib(options.gpu_memory_mib),
                      words.size_bytes() + flame.words.size() * 4};
        Buffer model(ctx, flame.words.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer histogram(ctx, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer counters(ctx, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        Buffer probes(ctx, words.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, budget);
        std::memcpy(model.mapped, flame.words.data(), flame.words.size() * 4);
        std::memcpy(probes.mapped, words.data(), words.size_bytes());
        model.flush();
        probes.flush();
        Bindings bindings(ctx, {&model, &histogram, &counters, &probes});
        PushConstants push;
        push.mode = 1;
        push.chain_count = static_cast<std::uint32_t>(count);
        ctx.begin();
        bindings.bind(push);
        barrier(ctx, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        ctx.submit(options.stop_token);
        probes.invalidate();
        std::vector<std::uint32_t> result(words.size());
        std::memcpy(result.data(), probes.mapped, words.size_bytes());
        return result;
    } catch (const Unavailable &) {
        discard_context();
        throw;
    }
}
} // namespace gpu
} // namespace apo

#else
namespace apo {
ComputeInfo compute_devices() {
    ComputeInfo result;
    result.diagnostic = "Vulkan support is disabled in this build";
    return result;
}
namespace gpu {
SampledHistogram sample(const Flame &, const RenderOptions &, const Geometry &, std::uint64_t) {
    throw Unavailable("Vulkan support is disabled in this build");
}
SampledHits sample_hits(const Flame &, const RenderOptions &options, const Geometry &,
                        std::uint64_t first_chain, std::uint32_t chain_count, std::uint64_t) {
    validate_hit_range(options, first_chain, chain_count);
    throw Unavailable("Vulkan support is disabled in this build");
}

std::vector<std::uint32_t> run_probes(const PackedFlame &, std::span<const std::uint32_t>,
                                      const RenderOptions &) {
    throw Unavailable("Vulkan support is disabled in this build");
}
} // namespace gpu
} // namespace apo
#endif
