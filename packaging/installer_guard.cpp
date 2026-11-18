// Private installer helper. The trusted file inventory is compiled into this
// executable; an editable on-disk manifest never authorizes a deletion.
// No user settings, recovery/queue data or arbitrary directory trees are removed.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "sha256.hpp"
#include "payload_manifest.hpp"

namespace {
namespace fs = std::filesystem;
using apo::detail::Sha256;
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  Handle() = default;
  explicit Handle(HANDLE v) : value(v) {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
  Handle &operator=(Handle &&other) noexcept {
    if (this != &other) { close(); value = std::exchange(other.value, INVALID_HANDLE_VALUE); }
    return *this;
  }
  ~Handle() { close(); }
  void close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(value, INVALID_HANDLE_VALUE)); }
  explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};
struct Key {
  HKEY value = nullptr;
  ~Key() { if (value) RegCloseKey(value); }
};
Handle log_file;
std::wstring registry_key() {
  return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ApophysisReborn-" +
         std::wstring(payload_version);
}
std::string utf8(const std::wstring &s) {
  if (s.empty()) return {};
  const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0, nullptr, nullptr);
  if (!size) throw std::runtime_error("Invalid Unicode text");
  std::string result(size, 0);
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(), size, nullptr, nullptr);
  return result;
}
std::wstring wide(const std::string &s) { return std::wstring(s.begin(), s.end()); }
void log(const std::wstring &s) {
  const auto bytes = utf8(s + L"\r\n");
  std::cout << bytes;
  if (log_file) {
    DWORD count = 0;
    WriteFile(log_file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr);
    FlushFileBuffers(log_file.value);
  }
}
[[noreturn]] void fail(const std::wstring &message, DWORD error = GetLastError()) {
  throw std::runtime_error(utf8(message) + " (Windows error " + std::to_string(error) + ")");
}
void require(bool test, const std::wstring &message) { if (!test) fail(message, ERROR_INVALID_DATA); }
bool missing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
bool same_path(const fs::path &a, const fs::path &b) {
  const auto x = a.lexically_normal().wstring(), y = b.lexically_normal().wstring();
  return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(), static_cast<int>(y.size()), TRUE) == CSTR_EQUAL;
}
BY_HANDLE_FILE_INFORMATION information(HANDLE file) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file, &info)) fail(L"Cannot inspect a file handle");
  return info;
}
std::uint64_t size_of(const BY_HANDLE_FILE_INFORMATION &info) {
  return (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
std::wstring upper(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return c >= L'a' && c <= L'z' ? c - 32 : c; });
  return s;
}
void component(const std::wstring &s) {
  require(!s.empty() && s != L"." && s != L".." && s.back() != L'.' && s.back() != L' ', L"Unsafe path component");
  require(std::none_of(s.begin(), s.end(), [](wchar_t c) { return c < 32 || std::wstring(L"<>:\"|?*").find(c) != std::wstring::npos; }),
          L"Unsupported Windows path character");
  const auto base = upper(s.substr(0, s.find(L'.')));
  require(base != L"CON" && base != L"PRN" && base != L"AUX" && base != L"NUL" && base != L"CONIN$" && base != L"CONOUT$" &&
          !(base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
            ((base[3] >= L'1' && base[3] <= L'9') || base[3] == L'\u00b9' || base[3] == L'\u00b2' || base[3] == L'\u00b3')),
          L"Reserved Windows device name");
}
fs::path absolute(const wchar_t *input, bool install_root = false) {
  std::wstring raw(input);
  std::replace(raw.begin(), raw.end(), L'/', L'\\');
  require(raw.size() > 3 && ((raw[0] >= L'A' && raw[0] <= L'Z') || (raw[0] >= L'a' && raw[0] <= L'z')) &&
          raw[1] == L':' && raw[2] == L'\\', L"Use an absolute local-drive path, not a network, device or drive-root path");
  const fs::path original(raw);
  for (const auto &part : original.relative_path()) component(part.wstring());
  std::array<wchar_t, 32768> text{};
  const auto count = GetFullPathNameW(raw.c_str(), static_cast<DWORD>(text.size()), text.data(), nullptr);
  require(count > 3 && count < text.size(), L"Invalid or overlong absolute path");
  fs::path result(std::wstring(text.data(), count));
  if (install_root) require(result.wstring().size() + longest_payload_path + 2 < 248,
                            L"Choose a shorter installation path");
  return result.lexically_normal();
}
struct Directories {
  std::map<std::wstring, Handle> held;
  std::vector<fs::path> created;
  void hold(const fs::path &directory, bool create) {
    fs::path current = directory.root_path();
    std::vector<fs::path> paths{current};
    for (const auto &part : directory.relative_path()) { current /= part; paths.push_back(current); }
    for (const auto &path : paths) {
      if (held.contains(path.wstring())) continue;
      auto attributes = GetFileAttributesW(path.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (!create || !missing(error)) fail(L"Cannot access directory: " + path.wstring(), error);
        if (!CreateDirectoryW(path.c_str(), nullptr)) fail(L"Cannot create directory: " + path.wstring());
        created.push_back(path);
      }
      Handle value(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      if (!value) fail(L"Cannot lock directory: " + path.wstring());
      const auto info = information(value.value);
      require((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
              L"Linked/reparse directories are refused: " + path.wstring());
      held.emplace(path.wstring(), std::move(value));
    }
  }
  void clear() { held.clear(); }
  void remove_created() {
    clear();
    for (auto it = created.rbegin(); it != created.rend(); ++it) RemoveDirectoryW(it->c_str());
  }
};
bool empty_directory(const fs::path &directory) {
  WIN32_FIND_DATAW entry{};
  const auto search = directory / L"*";
  const auto find = FindFirstFileW(search.c_str(), &entry);
  if (find == INVALID_HANDLE_VALUE) {
    if (missing(GetLastError())) return true;
    fail(L"Cannot enumerate installation directory");
  }
  bool empty = true;
  do {
    if (wcscmp(entry.cFileName, L".") && wcscmp(entry.cFileName, L"..")) { empty = false; break; }
  } while (FindNextFileW(find, &entry));
  const auto error = GetLastError();
  FindClose(find);
  if (empty && error != ERROR_NO_MORE_FILES) fail(L"Cannot finish enumerating the installation directory", error);
  return empty;
}
Handle read_file(const fs::path &path, bool deleting = false, bool allow_missing = false) {
  Handle result(CreateFileW(path.c_str(), GENERIC_READ | (deleting ? DELETE : 0), FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if (!result) {
    if (allow_missing && missing(GetLastError())) return {};
    fail(L"Cannot read or exclusively prepare file; close the application and retry: " + path.wstring());
  }
  const auto info = information(result.value);
  require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
          L"Linked/reparse/non-file entry is refused: " + path.wstring());
  return result;
}
std::string hash_file(HANDLE input) {
  LARGE_INTEGER zero{};
  if (!SetFilePointerEx(input, zero, nullptr, FILE_BEGIN)) fail(L"Cannot rewind input");
  Sha256 hash;
  std::array<unsigned char, 65536> buffer{};
  DWORD got = 0;
  for (;;) {
    if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) fail(L"Cannot read file bytes");
    if (!got) break;
    hash.add(buffer.data(), got);
  }
  return hash.finish();
}
void remove_handle(Handle &file) {
  FILE_DISPOSITION_INFO disposition{TRUE};
  if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)))
    fail(L"Cannot remove an owned file; close the application and retry");
  file.close();
}
struct Input { fs::path relative; std::string hash; std::uint64_t bytes; Handle file; };
std::wstring read_value(HKEY key, const wchar_t *name, bool optional = false) {
  DWORD type = 0, size = 0;
  auto result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
  if (optional && result == ERROR_FILE_NOT_FOUND) return {};
  require(result == ERROR_SUCCESS && type == REG_SZ && size >= 2 && size <= 65536 && size % 2 == 0,
          L"Installation registration is missing or invalid");
  std::wstring text(size / 2, L'\0');
  result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(text.data()), &size);
  require(result == ERROR_SUCCESS && text.back() == 0, L"Cannot read installation registration");
  text.pop_back();
  require(text.find(L'\0') == std::wstring::npos, L"Invalid embedded registry terminator");
  return text;
}
void write_value(HKEY key, const wchar_t *name, const std::wstring &text) {
  const auto result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(text.c_str()),
                                   static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
  if (result != ERROR_SUCCESS) fail(L"Cannot register this per-user installation", result);
}
void write_number(HKEY key, const wchar_t *name, DWORD value) {
  const auto result = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
  if (result != ERROR_SUCCESS) fail(L"Cannot register this per-user installation", result);
}
fs::path known_folder(REFKNOWNFOLDERID id) {
  wchar_t *text = nullptr;
  const auto hr = SHGetKnownFolderPath(id, 0, nullptr, &text);
  if (FAILED(hr)) fail(L"Cannot locate the current user's shortcut folder", static_cast<DWORD>(hr));
  fs::path result(text);
  CoTaskMemFree(text);
  return absolute(result.c_str());
}
const std::array<const wchar_t *, 2> shortcut_names{L"StartMenuSHA256", L"DesktopSHA256"};
fs::path shortcut_path(std::size_t index) {
  const auto name = L"Apophysis Reborn " + std::wstring(payload_version) + L".lnk";
  return index == 0 ? known_folder(FOLDERID_Programs) / L"Apophysis Reborn" / name
                    : known_folder(FOLDERID_Desktop) / name;
}
Handle create_shortcut(const fs::path &path, const fs::path &target, Directories &directories) {
  directories.hold(path.parent_path(), true);
  require(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && missing(GetLastError()),
          L"A shortcut with this version's name already exists: " + path.wstring());
  IShellLinkW *link = nullptr;
  auto hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link));
  if (FAILED(hr)) fail(L"Cannot create a Windows shortcut", static_cast<DWORD>(hr));
  IPersistFile *persist = nullptr;
  fs::path temporary;
  try {
    if (FAILED(link->SetPath(target.c_str())) || FAILED(link->SetWorkingDirectory(target.parent_path().c_str())) ||
        FAILED(link->SetDescription(L"Apophysis Reborn fractal flame editor"))) fail(L"Cannot prepare Windows shortcut");
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist));
    if (FAILED(hr)) fail(L"Cannot persist Windows shortcut", static_cast<DWORD>(hr));
    GUID id{}; CoCreateGuid(&id);
    std::array<wchar_t, 50> guid{};
    StringFromGUID2(id, guid.data(), static_cast<int>(guid.size()));
    temporary = path.parent_path() / (L".apo-link-" + std::wstring(guid.data()) + L".tmp");
    Handle reserved(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!reserved) fail(L"Cannot reserve a shortcut staging file");
    reserved.close();
    hr = persist->Save(temporary.c_str(), TRUE);
    if (FAILED(hr)) fail(L"Cannot save Windows shortcut", static_cast<DWORD>(hr));
    // MoveFile refuses an existing destination; no shortcut is overwritten.
    if (!MoveFileW(temporary.c_str(), path.c_str())) fail(L"Shortcut destination appeared during installation");
    temporary.clear();
    persist->Release(); persist = nullptr;
    link->Release(); link = nullptr;
    return read_file(path, true);
  } catch (...) {
    if (persist) persist->Release();
    if (link) link->Release();
    if (!temporary.empty()) DeleteFileW(temporary.c_str());
    throw;
  }
}
void check_registry_absent() {
  HKEY key = nullptr;
  const auto result = RegOpenKeyExW(HKEY_CURRENT_USER, registry_key().c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
  if (key) RegCloseKey(key);
  require(result == ERROR_FILE_NOT_FOUND, L"This version is already registered. Uninstall it before reinstalling; other versions may coexist");
}
void check_destination(const fs::path &root) {
  Directories directories;
  auto parent = root;
  while (GetFileAttributesW(parent.c_str()) == INVALID_FILE_ATTRIBUTES && missing(GetLastError())) {
    require(parent != parent.root_path(), L"Installation drive does not exist");
    parent = parent.parent_path();
  }
  directories.hold(parent, false);
  if (GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES)
    require(empty_directory(root), L"Choose a new or empty installation folder. Existing files will not be overwritten");
  check_registry_absent();
}
void install(const fs::path &source, const fs::path &uninstaller, const fs::path &root, bool menu, bool desktop) {
  check_destination(root);
  Directories source_directories, destination_directories;
  std::vector<Input> inputs;
  std::uint64_t total = 0;
  for (const auto &entry : payload_entries) {
    const fs::path relative(entry.path);
    source_directories.hold((source / relative).parent_path(), false);
    auto input = read_file(source / relative);
    require(size_of(information(input.value)) == entry.bytes && hash_file(input.value) == entry.hash,
            L"Installer payload is missing or changed: " + relative.wstring());
    inputs.push_back({relative, entry.hash, entry.bytes, std::move(input)});
    total += entry.bytes;
  }
  source_directories.hold(uninstaller.parent_path(), false);
  auto uninstaller_file = read_file(uninstaller);
  const auto uninstaller_hash = hash_file(uninstaller_file.value);
  inputs.push_back({L"Uninstall.exe", uninstaller_hash, size_of(information(uninstaller_file.value)), std::move(uninstaller_file)});
  std::vector<std::pair<fs::path, Handle>> created_files;
  Key registration;
  bool new_registration = false;
  try {
    destination_directories.hold(root, true);
    require(empty_directory(root), L"Installation destination changed; no files were replaced");
    for (auto &input : inputs) {
      const auto path = root / input.relative;
      destination_directories.hold(path.parent_path(), true);
      Handle output(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      if (!output) fail(L"Destination appeared or cannot be created: " + path.wstring());
      created_files.emplace_back(path, std::move(output));
      auto handle = created_files.back().second.value;
      LARGE_INTEGER zero{};
      if (!SetFilePointerEx(input.file.value, zero, nullptr, FILE_BEGIN)) fail(L"Cannot rewind payload file");
      Sha256 hash;
      std::uint64_t written = 0;
      std::array<unsigned char, 65536> data{};
      for (;;) {
        DWORD got = 0, put = 0;
        if (!ReadFile(input.file.value, data.data(), static_cast<DWORD>(data.size()), &got, nullptr)) fail(L"Cannot read staged payload");
        if (!got) break;
        if (!WriteFile(handle, data.data(), got, &put, nullptr) || got != put) fail(L"Cannot write installed payload");
        hash.add(data.data(), got); written += got;
      }
      require(written == input.bytes && hash.finish() == input.hash, L"Payload changed during copying");
      if (!FlushFileBuffers(handle)) fail(L"Cannot flush installed payload");
    }
    std::array<std::wstring, 2> shortcut_hashes{};
    for (std::size_t i = 0; i < shortcut_names.size(); ++i) {
      if (!(i == 0 ? menu : desktop)) continue;
      const auto path = shortcut_path(i);
      auto shortcut = create_shortcut(path, root / L"apophysis-reborn.exe", destination_directories);
      shortcut_hashes[i] = wide(hash_file(shortcut.value));
      created_files.emplace_back(path, std::move(shortcut));
    }
    DWORD disposition = 0;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, registry_key().c_str(), 0, nullptr, 0,
        KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &registration.value, &disposition);
    if (result != ERROR_SUCCESS) fail(L"Cannot register installation", result);
    require(disposition == REG_CREATED_NEW_KEY, L"Installation registration appeared concurrently; preserving it");
    new_registration = true;
    write_value(registration.value, L"DisplayName", L"Apophysis Reborn " + std::wstring(payload_version));
    write_value(registration.value, L"DisplayVersion", payload_version);
    write_value(registration.value, L"Publisher", L"Apophysis Reborn project");
    write_value(registration.value, L"InstallLocation", root.wstring());
    write_value(registration.value, L"DisplayIcon", (root / L"apophysis-reborn.exe").wstring());
    write_value(registration.value, L"UninstallString", L"\"" + (root / L"Uninstall.exe").wstring() + L"\"");
    write_value(registration.value, L"QuietUninstallString", L"\"" + (root / L"Uninstall.exe").wstring() + L"\" /S");
    write_value(registration.value, L"PayloadSHA256", payload_manifest_hash);
    write_value(registration.value, L"UninstallerSHA256", wide(uninstaller_hash));
    write_value(registration.value, L"StartMenuSHA256", shortcut_hashes[0]);
    write_value(registration.value, L"DesktopSHA256", shortcut_hashes[1]);
    write_number(registration.value, L"EstimatedSize", static_cast<DWORD>((total + 1023) / 1024));
    write_number(registration.value, L"NoModify", 1); write_number(registration.value, L"NoRepair", 1);
    if (RegFlushKey(registration.value) != ERROR_SUCCESS) fail(L"Cannot flush installation registration");
    log(L"Installed Apophysis Reborn " + std::wstring(payload_version) + L" in " + root.wstring());
    log(L"Other versions, user settings, recovery data and personal projects were preserved.");
  } catch (...) {
    if (registration.value) RegCloseKey(std::exchange(registration.value, nullptr));
    if (new_registration) RegDeleteKeyExW(HKEY_CURRENT_USER, registry_key().c_str(), KEY_WOW64_64KEY, 0);
    for (auto &[path, file] : created_files) {
      try { remove_handle(file); } catch (...) { log(L"Rollback retained a file that could not be removed: " + path.wstring()); }
    }
    created_files.clear(); destination_directories.remove_created();
    throw;
  }
}
int uninstall(const fs::path &root) {
  Directories directories;
  directories.hold(root, false);
  Key registration;
  auto result = RegOpenKeyExW(HKEY_CURRENT_USER, registry_key().c_str(), 0,
                            KEY_QUERY_VALUE | KEY_WOW64_64KEY, &registration.value);
  if (result != ERROR_SUCCESS) fail(L"This installation has no matching per-user registration", result);
  require(same_path(absolute(read_value(registration.value, L"InstallLocation").c_str(), true), root) &&
          read_value(registration.value, L"PayloadSHA256") == payload_manifest_hash,
          L"This uninstaller does not match the registered folder and payload");
  std::vector<std::pair<fs::path, std::string>> candidates;
  std::set<fs::path> folders{root};
  for (const auto &entry : payload_entries) {
    const fs::path path = root / entry.path;
    candidates.emplace_back(path, entry.hash);
    for (auto parent = path.parent_path(); parent != root && parent != parent.root_path(); parent = parent.parent_path())
      folders.insert(parent);
  }
  const auto uninstall_hash = read_value(registration.value, L"UninstallerSHA256");
  require(uninstall_hash.size() == 64, L"Invalid uninstaller identity");
  candidates.emplace_back(root / L"Uninstall.exe", utf8(uninstall_hash));
  fs::path menu_folder;
  for (std::size_t i = 0; i < shortcut_names.size(); ++i) {
    const auto expected = read_value(registration.value, shortcut_names[i]);
    if (!expected.empty()) {
      require(expected.size() == 64, L"Invalid shortcut identity");
      const auto path = shortcut_path(i);
      candidates.emplace_back(path, utf8(expected));
      if (i == 0) menu_folder = path.parent_path();
    }
  }
  std::vector<std::pair<fs::path, Handle>> ready;
  bool retained = false;
  // Open and verify every remaining owned file before deleting the first one.
  // The open handles deny writes/deletion; directory handles deny replacement.
  for (const auto &[path, expected] : candidates) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && missing(GetLastError())) continue;
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))) {
      retained = true; log(L"Retained a linked, read-only or non-file replacement: " + path.wstring()); continue;
    }
    directories.hold(path.parent_path(), false);
    auto file = read_file(path, true, true);
    if (!file) continue;
    const auto info = information(file.value);
    if (info.nNumberOfLinks != 1 || (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) || hash_file(file.value) != expected) {
      retained = true; log(L"Retained modified, read-only or hard-linked file: " + path.wstring()); continue;
    }
    ready.emplace_back(path, std::move(file));
  }
  for (auto &[path, file] : ready) { remove_handle(file); log(L"Removed: " + path.wstring()); }
  RegCloseKey(std::exchange(registration.value, nullptr));
  result = RegDeleteKeyExW(HKEY_CURRENT_USER, registry_key().c_str(), KEY_WOW64_64KEY, 0);
  if (result != ERROR_SUCCESS) fail(L"Application files were removed but the uninstall registration could not be removed", result);
  directories.clear();
  std::vector<fs::path> ordered(folders.begin(), folders.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.native().size() > b.native().size(); });
  for (const auto &path : ordered) RemoveDirectoryW(path.c_str()); // Never recursive.
  // A shared Start Menu folder is removed only when completely empty.
  if (!menu_folder.empty()) RemoveDirectoryW(menu_folder.c_str());
  if (GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES) retained = true;
  log(retained ? L"Uninstalled. Modified or additional files remain in their original locations."
               : L"Uninstalled the versioned program payload.");
  log(L"User preferences, projects, recovery data, render checkpoints and other versions were not removed.");
  return retained ? 10 : 0;
}
void open_log(const wchar_t *name) {
  const auto path = absolute(name);
  Directories directory;
  directory.hold(path.parent_path(), false);
  log_file = Handle(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!log_file) fail(L"Cannot open the reserved installer log");
  const auto info = information(log_file.value);
  require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks == 1,
          L"The installer log is not an ordinary private file");
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  try {
    if (argc == 3 && wcscmp(argv[1], L"check") == 0) check_destination(absolute(argv[2], true));
    else if (argc == 8 && wcscmp(argv[1], L"install") == 0) {
      require((wcscmp(argv[5], L"0") == 0 || wcscmp(argv[5], L"1") == 0) &&
              (wcscmp(argv[6], L"0") == 0 || wcscmp(argv[6], L"1") == 0), L"Invalid shortcut selection");
      open_log(argv[7]);
      install(absolute(argv[2]), absolute(argv[3]), absolute(argv[4], true), argv[5][0] == L'1', argv[6][0] == L'1');
    } else if (argc == 4 && wcscmp(argv[1], L"uninstall") == 0) {
      open_log(argv[3]);
      const auto result = uninstall(absolute(argv[2], true));
      if (SUCCEEDED(initialized)) CoUninitialize();
      return result;
    } else throw std::runtime_error("Invalid private installer-helper arguments");
    if (SUCCEEDED(initialized)) CoUninitialize();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    if (log_file) {
      const auto text = std::string("FAILED: ") + error.what() + "\r\n";
      DWORD count = 0; WriteFile(log_file.value, text.data(), static_cast<DWORD>(text.size()), &count, nullptr);
      FlushFileBuffers(log_file.value);
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return 2;
  }
}
