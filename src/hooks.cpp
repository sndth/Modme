#include "hooks.h"
#include "archive_hooks.h"
#include "log.h"

#include <MinHook.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

struct merged_listing
{
  std::vector<WIN32_FIND_DATAW> entries;
  size_t next = 0;
};

static const DWORD write_access = GENERIC_WRITE | GENERIC_ALL |
                                  MAXIMUM_ALLOWED | FILE_WRITE_DATA |
                                  FILE_APPEND_DATA | DELETE;

static std::wstring game_root;
static std::atomic<std::shared_ptr<const scanned_mods>> mods_state;
static std::mutex logged_mutex;
static std::unordered_set<std::wstring> logged;
static std::mutex listings_mutex;
static std::unordered_map<HANDLE, std::unique_ptr<merged_listing>> listings;

static decltype(CreateFileW)* orig_create_file_w;
static decltype(CreateFileA)* orig_create_file_a;
static decltype(GetFileAttributesW)* orig_get_file_attributes_w;
static decltype(GetFileAttributesA)* orig_get_file_attributes_a;
static decltype(GetFileAttributesExW)* orig_get_file_attributes_ex_w;
static decltype(GetFileAttributesExA)* orig_get_file_attributes_ex_a;
static decltype(FindFirstFileW)* orig_find_first_file_w;
static decltype(FindFirstFileA)* orig_find_first_file_a;
static decltype(FindFirstFileExW)* orig_find_first_file_ex_w;
static decltype(FindNextFileW)* orig_find_next_file_w;
static decltype(FindNextFileA)* orig_find_next_file_a;
static decltype(FindClose)* orig_find_close;
static decltype(LoadLibraryExW)* orig_load_library_ex_w;

static void
log_once(const wchar_t* format, const mod_file& file)
{
  if (file.folder) {
    return;
  }

  std::wstring line =
    std::vformat(format, std::make_wformat_args(file.name, file.mod));

  {
    std::lock_guard lock(logged_mutex);

    if (!logged.insert(line).second) {
      return;
    }
  }

  write_log(line);
}

static std::wstring
full_path(const wchar_t* path)
{
  wchar_t full[MAX_PATH * 2]{};
  const DWORD len =
    path ? GetFullPathNameW(path, DWORD(std::size(full)), full, nullptr) : 0;

  return len < std::size(full) ? std::wstring(full, len) : std::wstring();
}

static std::wstring
mod_key(const std::wstring& path)
{
  if (path.starts_with(game_root)) {
    return path.substr(game_root.size());
  }

  if (path.size() > 3 && path[1] == L':' && path[2] == L'\\') {
    return path.substr(3);
  }

  return {};
}

static const wchar_t*
redirect(const scanned_mods& mods, const wchar_t* path, DWORD access)
{
  const std::wstring full = full_path(path);

  if (full.empty()) {
    return path;
  }

  const std::wstring lowered = lower(full);
  auto it = mods.files.find(mod_key(lowered));

  if (it == mods.files.end()) {
    return path;
  }

  if (!lowered.starts_with(game_root) &&
      orig_get_file_attributes_w(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
    log_once(L"Skipped {} from {}, exists outside the game folder", it->second);
    return path;
  }

  if (access & write_access) {
    log_once(L"Skipped {} from {}, opened for writing", it->second);
    return path;
  }

  log_once(L"Replaced {} from {}", it->second);
  return it->second.path.c_str();
}

static bool
to_wide(const char* path, std::span<wchar_t> wide)
{
  return path && MultiByteToWideChar(
                   CP_ACP, 0, path, -1, wide.data(), int(wide.size())) != 0;
}

static void
to_find_data_a(const WIN32_FIND_DATAW& found, LPWIN32_FIND_DATAA data)
{
  data->dwFileAttributes = found.dwFileAttributes;
  data->ftCreationTime = found.ftCreationTime;
  data->ftLastAccessTime = found.ftLastAccessTime;
  data->ftLastWriteTime = found.ftLastWriteTime;
  data->nFileSizeHigh = found.nFileSizeHigh;
  data->nFileSizeLow = found.nFileSizeLow;
  data->dwReserved0 = found.dwReserved0;
  data->dwReserved1 = found.dwReserved1;
  WideCharToMultiByte(CP_ACP,
                      0,
                      found.cFileName,
                      -1,
                      data->cFileName,
                      int(std::size(data->cFileName)),
                      nullptr,
                      nullptr);
  WideCharToMultiByte(CP_ACP,
                      0,
                      found.cAlternateFileName,
                      -1,
                      data->cAlternateFileName,
                      int(std::size(data->cAlternateFileName)),
                      nullptr,
                      nullptr);
}

static bool
hidden_from_mod(const WIN32_FIND_DATAW& data, const std::wstring& name)
{
  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    return name.ends_with(L".img");
  }

  return name.ends_with(L".asi");
}

static void
list_into(const std::wstring& pattern,
          bool from_mod,
          std::map<std::wstring, WIN32_FIND_DATAW>& entries)
{
  WIN32_FIND_DATAW data{};
  HANDLE handle = orig_find_first_file_ex_w(pattern.c_str(),
                                            FindExInfoStandard,
                                            &data,
                                            FindExSearchNameMatch,
                                            nullptr,
                                            0);

  if (handle == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    std::wstring name = lower(data.cFileName);

    if (!from_mod || !hidden_from_mod(data, name)) {
      entries.try_emplace(std::move(name), data);
    }
  } while (orig_find_next_file_w(handle, &data));

  orig_find_close(handle);
}

static std::optional<HANDLE>
list_merged(const scanned_mods& mods,
            const wchar_t* pattern,
            LPWIN32_FIND_DATAW data)
{
  const std::wstring full = full_path(pattern);
  const std::wstring lowered = lower(full);
  const std::wstring key = mod_key(lowered);
  const size_t slash = key.rfind(L'\\');
  const std::wstring folder =
    slash == std::wstring::npos ? std::wstring() : key.substr(0, slash);
  const bool inside = !full.empty() && lowered.starts_with(game_root);
  const auto it = mods.files.find(folder);
  const bool mod_folder =
    folder.empty() ? inside : it != mods.files.end() && it->second.folder;

  if (!mod_folder ||
      key.find_first_of(L"*?", slash + 1) == std::wstring::npos) {
    return std::nullopt;
  }

  std::map<std::wstring, WIN32_FIND_DATAW> entries;

  if (!inside) {
    list_into(full, false, entries);
  }

  for (const fs::path& mod : mods.roots) {
    list_into((mod / key).native(), true, entries);
  }

  if (inside) {
    list_into(full, false, entries);
  }

  if (entries.empty()) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }

  auto listing = std::make_unique<merged_listing>();

  for (const auto& [name, entry] : entries) {
    listing->entries.push_back(entry);
  }

  *data = listing->entries.front();
  listing->next = 1;

  const HANDLE handle = listing.get();
  std::lock_guard lock(listings_mutex);

  listings.emplace(handle, std::move(listing));

  return handle;
}

static std::optional<BOOL>
next_listed(HANDLE handle, LPWIN32_FIND_DATAW data)
{
  std::lock_guard lock(listings_mutex);
  auto it = listings.find(handle);

  if (it == listings.end()) {
    return std::nullopt;
  }

  merged_listing& listing = *it->second;

  if (listing.next == listing.entries.size()) {
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
  }

  *data = listing.entries[listing.next++];

  return TRUE;
}

static HANDLE WINAPI
hook_create_file_w(LPCWSTR name,
                   DWORD access,
                   DWORD share,
                   LPSECURITY_ATTRIBUTES sa,
                   DWORD disposition,
                   DWORD flags,
                   HANDLE tmpl)
{
  HANDLE handle = orig_create_file_w(redirect(*current_mods(), name, access),
                                     access,
                                     share,
                                     sa,
                                     disposition,
                                     flags,
                                     tmpl);

  if (handle != INVALID_HANDLE_VALUE && !(access & write_access)) {
    const DWORD error = GetLastError();

    track_archive(handle, name, flags);
    SetLastError(error);
  }

  return handle;
}

static HANDLE WINAPI
hook_create_file_a(LPCSTR name,
                   DWORD access,
                   DWORD share,
                   LPSECURITY_ATTRIBUTES sa,
                   DWORD disposition,
                   DWORD flags,
                   HANDLE tmpl)
{
  wchar_t wide[MAX_PATH * 2]{};

  if (!to_wide(name, wide)) {
    return orig_create_file_a(
      name, access, share, sa, disposition, flags, tmpl);
  }

  return hook_create_file_w(wide, access, share, sa, disposition, flags, tmpl);
}

static DWORD WINAPI
hook_get_file_attributes_w(LPCWSTR name)
{
  return orig_get_file_attributes_w(redirect(*current_mods(), name, 0));
}

static DWORD WINAPI
hook_get_file_attributes_a(LPCSTR name)
{
  wchar_t wide[MAX_PATH * 2]{};

  if (!to_wide(name, wide)) {
    return orig_get_file_attributes_a(name);
  }

  return hook_get_file_attributes_w(wide);
}

static BOOL WINAPI
hook_get_file_attributes_ex_w(LPCWSTR name,
                              GET_FILEEX_INFO_LEVELS level,
                              LPVOID info)
{
  return orig_get_file_attributes_ex_w(
    redirect(*current_mods(), name, 0), level, info);
}

static BOOL WINAPI
hook_get_file_attributes_ex_a(LPCSTR name,
                              GET_FILEEX_INFO_LEVELS level,
                              LPVOID info)
{
  wchar_t wide[MAX_PATH * 2]{};

  if (!to_wide(name, wide)) {
    return orig_get_file_attributes_ex_a(name, level, info);
  }

  return hook_get_file_attributes_ex_w(wide, level, info);
}

static HANDLE WINAPI
hook_find_first_file_ex_w(LPCWSTR name,
                          FINDEX_INFO_LEVELS level,
                          LPVOID data,
                          FINDEX_SEARCH_OPS search,
                          LPVOID filter,
                          DWORD flags)
{
  const std::shared_ptr<const scanned_mods> mods = current_mods();
  const wchar_t* target = redirect(*mods, name, 0);

  if (target != name) {
    return orig_find_first_file_ex_w(
      target, level, data, search, filter, flags);
  }

  if (auto merged =
        list_merged(*mods, name, static_cast<LPWIN32_FIND_DATAW>(data))) {
    return *merged;
  }

  return orig_find_first_file_ex_w(name, level, data, search, filter, flags);
}

static HANDLE WINAPI
hook_find_first_file_w(LPCWSTR name, LPWIN32_FIND_DATAW data)
{
  return hook_find_first_file_ex_w(
    name, FindExInfoStandard, data, FindExSearchNameMatch, nullptr, 0);
}

static HANDLE WINAPI
hook_find_first_file_a(LPCSTR name, LPWIN32_FIND_DATAA data)
{
  wchar_t wide[MAX_PATH * 2]{};

  if (!to_wide(name, wide)) {
    return orig_find_first_file_a(name, data);
  }

  WIN32_FIND_DATAW found{};
  HANDLE handle = hook_find_first_file_w(wide, &found);

  if (handle != INVALID_HANDLE_VALUE) {
    to_find_data_a(found, data);
  }

  return handle;
}

static BOOL WINAPI
hook_find_next_file_w(HANDLE handle, LPWIN32_FIND_DATAW data)
{
  if (auto listed = next_listed(handle, data)) {
    return *listed;
  }

  return orig_find_next_file_w(handle, data);
}

static BOOL WINAPI
hook_find_next_file_a(HANDLE handle, LPWIN32_FIND_DATAA data)
{
  WIN32_FIND_DATAW found{};

  if (auto listed = next_listed(handle, &found)) {
    if (*listed) {
      to_find_data_a(found, data);
    }

    return *listed;
  }

  return orig_find_next_file_a(handle, data);
}

static BOOL WINAPI
hook_find_close(HANDLE handle)
{
  {
    std::lock_guard lock(listings_mutex);

    if (listings.erase(handle)) {
      return TRUE;
    }
  }

  return orig_find_close(handle);
}

static HMODULE WINAPI
hook_load_library_ex_w(LPCWSTR name, HANDLE file, DWORD flags)
{
  const bool has_folder =
    name && std::wstring_view(name).find_first_of(L"\\/") != std::wstring::npos;

  return orig_load_library_ex_w(
    has_folder ? redirect(*current_mods(), name, 0) : name, file, flags);
}

size_t
create_api_hooks(std::span<const api_hook> hooks)
{
  size_t created = 0;

  for (const api_hook& hook : hooks) {
    MH_STATUS status = MH_ERROR_FUNCTION_NOT_FOUND;

    for (const wchar_t* module_name : { L"kernelbase", L"kernel32" }) {
      HMODULE module = GetModuleHandleW(module_name);
      FARPROC target = module ? GetProcAddress(module, hook.name) : nullptr;

      if (!target) {
        continue;
      }

      if (!*hook.original) {
        *hook.original = reinterpret_cast<void*>(target);
      }

      status = MH_CreateHook(reinterpret_cast<LPVOID>(target),
                             hook.detour,
                             reinterpret_cast<LPVOID*>(hook.original));

      if (status == MH_OK) {
        break;
      }
    }

    if (status == MH_OK) {
      ++created;
    } else {
      log_line(
        L"Hook {}: {}", widen(hook.name), widen(MH_StatusToString(status)));
    }
  }

  return created;
}

std::wstring
game_file(const wchar_t* path)
{
  const std::wstring lowered = lower(full_path(path));

  return lowered.starts_with(game_root) ? lowered.substr(game_root.size())
                                        : std::wstring();
}

HANDLE
open_for_reading(const wchar_t* path)
{
  return orig_create_file_w(path,
                            GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr);
}

HANDLE
open_game_file(const std::wstring& file)
{
  const std::wstring path = game_root + file;

  return open_for_reading(redirect(*current_mods(), path.c_str(), 0));
}

std::shared_ptr<const scanned_mods>
current_mods()
{
  return mods_state.load();
}

void
set_mods(scanned_mods mods)
{
  mods_state.store(std::make_shared<const scanned_mods>(std::move(mods)));
}

void
install_file_hooks(const std::filesystem::path& root, scanned_mods mods)
{
  game_root = lower(root.native()) + L'\\';
  set_mods(std::move(mods));

  const api_hook hooks[] = {
    { "CreateFileW",
      &hook_create_file_w,
      reinterpret_cast<void**>(&orig_create_file_w) },
    { "CreateFileA",
      &hook_create_file_a,
      reinterpret_cast<void**>(&orig_create_file_a) },
    { "GetFileAttributesW",
      &hook_get_file_attributes_w,
      reinterpret_cast<void**>(&orig_get_file_attributes_w) },
    { "GetFileAttributesA",
      &hook_get_file_attributes_a,
      reinterpret_cast<void**>(&orig_get_file_attributes_a) },
    { "GetFileAttributesExW",
      &hook_get_file_attributes_ex_w,
      reinterpret_cast<void**>(&orig_get_file_attributes_ex_w) },
    { "GetFileAttributesExA",
      &hook_get_file_attributes_ex_a,
      reinterpret_cast<void**>(&orig_get_file_attributes_ex_a) },
    { "FindFirstFileW",
      &hook_find_first_file_w,
      reinterpret_cast<void**>(&orig_find_first_file_w) },
    { "FindFirstFileA",
      &hook_find_first_file_a,
      reinterpret_cast<void**>(&orig_find_first_file_a) },
    { "FindFirstFileExW",
      &hook_find_first_file_ex_w,
      reinterpret_cast<void**>(&orig_find_first_file_ex_w) },
    { "FindNextFileW",
      &hook_find_next_file_w,
      reinterpret_cast<void**>(&orig_find_next_file_w) },
    { "FindNextFileA",
      &hook_find_next_file_a,
      reinterpret_cast<void**>(&orig_find_next_file_a) },
    { "FindClose",
      &hook_find_close,
      reinterpret_cast<void**>(&orig_find_close) },
    { "LoadLibraryExW",
      &hook_load_library_ex_w,
      reinterpret_cast<void**>(&orig_load_library_ex_w) },
  };

  MH_STATUS status = MH_Initialize();

  if (status != MH_OK) {
    log_line(L"Hooks: {}", widen(MH_StatusToString(status)));
    return;
  }

  const size_t created =
    create_api_hooks(hooks) + create_api_hooks(archive_hooks());
  status = MH_EnableHook(MH_ALL_HOOKS);

  if (status != MH_OK) {
    log_line(L"Hooks: {}", widen(MH_StatusToString(status)));
    return;
  }

  log_line(L"Hooks: {}/{}", created, std::size(hooks) + archive_hooks().size());
}
