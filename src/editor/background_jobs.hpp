#pragma once

#include "gpu.hpp"
#include <QApplicationStatic>
#include <QMetaObject>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace apo::editor {

// A serial worker survives the windows that submit jobs. Keep only the latest
// waiting job per owner, and remove closed owners even if the driver is blocked.
class BackgroundJobs {
  public:
    using Task = std::function<void(std::stop_token)>;

    void submit(const void *owner, Task task) {
        std::lock_guard lock(mutex_);
        if (closing_)
            return;
        if (!thread_.joinable())
            thread_ = std::thread([this] { run(); });
        cancelLocked(owner);
        pending_.push_back({owner, std::move(task)});
        wake_.notify_one();
    }

    void cancel(const void *owner) {
        std::lock_guard lock(mutex_);
        cancelLocked(owner);
    }

    // Called at application shutdown, after normal dialog destruction. No
    // renderer or GPU resources can outlive the application and its plugins.
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            closing_ = true;
            pending_.clear();
            activeStop_.request_stop();
        }
        wake_.notify_one();
        if (thread_.joinable())
            thread_.join();
    }

  private:
    struct Job {
        const void *owner;
        Task task;
    };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> pending_;
    const void *activeOwner_ = nullptr;
    std::stop_source activeStop_;
    bool closing_ = false;
    std::thread thread_;

    void cancelLocked(const void *owner) {
        std::erase_if(pending_, [owner](const Job &job) { return job.owner == owner; });
        if (activeOwner_ == owner)
            activeStop_.request_stop();
    }

    void run() {
        gpu::ContextScope contextScope;
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return closing_ || !pending_.empty(); });
            if (closing_)
                return;
            auto job = std::move(pending_.front());
            pending_.pop_front();
            activeOwner_ = job.owner;
            activeStop_ = std::stop_source();
            const auto stop = activeStop_.get_token();
            lock.unlock();
            job.task(stop);
            // Release snapshots and plugin handles outside the queue mutex.
            job.task = {};
            lock.lock();
            activeOwner_ = nullptr;
        }
    }
};

// Q_APPLICATION_STATIC invokes this owner's destructor while Qt and the
// renderer are still available. A controller parented to the application can
// safely cancel its subscription even if the queue has already shut down.
struct ApplicationJobs {
    std::shared_ptr<BackgroundJobs> jobs = std::make_shared<BackgroundJobs>();
    ~ApplicationJobs() { jobs->shutdown(); }
};

// Revocation and posting take the same short lock. Once close() returns, a
// background task has no path to a destroyed QObject. Qt discards events
// already posted to that receiver. No worker reads a QPointer across threads.
template <class Target> class BackgroundReceiver {
  public:
    explicit BackgroundReceiver(Target *target) : target_(target) {}

    void close() {
        std::lock_guard lock(mutex_);
        target_ = nullptr;
    }

    template <class Callback> void post(Callback callback) {
        std::lock_guard lock(mutex_);
        if (target_)
            QMetaObject::invokeMethod(
                target_, [target = target_, callback = std::move(callback)]() mutable {
                    callback(target);
                }, Qt::QueuedConnection);
    }

  private:
    std::mutex mutex_;
    Target *target_;
};
} // namespace apo::editor
