#include "hooks.h"
#include "log.h"

#include <MinHook.h>
#include <mutex>
#include <unordered_set>

static const DWORD write_access = GENERIC_WRITE | GENERIC_ALL |
                                  MAXIMUM_ALLOWED | FILE_WRITE_DATA |
                                  FILE_APPEND_DATA | DELETE;

static std::wstring game_root;
static file_overrides overrides;
static std::mutex logged_mutex;
static std::unordered_set<std::wstring> logged;

static decltype(CreateFileW)* orig_create_file_w;
static decltype(CreateFileA)* orig_create_file_a;
static decltype(GetFileAttributesW)* orig_get_file_attributes_w;
static decltype(GetFileAttributesA)* orig_get_file_attributes_a;
static decltype(GetFileAttributesExW)* orig_get_file_attributes_ex_w;
static decltype(GetFileAttributesExA)* orig_get_file_attributes_ex_a;
static decltype(FindFirstFileW)* orig_find_first_file_w;
static decltype(FindFirstFileA)* orig_find_first_file_a;

static void
log_once(std::wstring line)
{
  {
    std::lock_guard lock(logged_mutex);

    if (!logged.insert(line).second) {
      return;
    }
  }

  write_log(line);
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
redirect(const wchar_t* path, DWORD access)
{
  if (!path) {
    return path;
  }

  wchar_t full[MAX_PATH * 2]{};
  DWORD len = GetFullPathNameW(path, DWORD(std::size(full)), full, nullptr);

  if (len == 0 || len >= std::size(full)) {
    return path;
  }

  const std::wstring lowered = lower({ full, len });
  const std::wstring key = mod_key(lowered);
  auto it = overrides.find(key);

  if (it == overrides.end()) {
    return path;
  }

  if (!lowered.starts_with(game_root) &&
      orig_get_file_attributes_w(full) != INVALID_FILE_ATTRIBUTES) {
    log_once(std::format(L"Skipped {} from {}, exists outside the game folder",
                         it->second.name,
                         it->second.mod));
    return path;
  }

  if (access & write_access) {
    log_once(std::format(L"Skipped {} from {}, opened for writing",
                         it->second.name,
                         it->second.mod));
    return path;
  }

  log_once(
    std::format(L"Replaced {} from {}", it->second.name, it->second.mod));
  return it->second.path.c_str();
}

static bool
to_wide(const char* path, std::span<wchar_t> wide)
{
  return path && MultiByteToWideChar(
                   CP_ACP, 0, path, -1, wide.data(), int(wide.size())) != 0;
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
  return orig_create_file_w(
    redirect(name, access), access, share, sa, disposition, flags, tmpl);
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
  return orig_get_file_attributes_w(redirect(name, 0));
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
  return orig_get_file_attributes_ex_w(redirect(name, 0), level, info);
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
hook_find_first_file_w(LPCWSTR name, LPWIN32_FIND_DATAW data)
{
  return orig_find_first_file_w(redirect(name, 0), data);
}

static HANDLE WINAPI
hook_find_first_file_a(LPCSTR name, LPWIN32_FIND_DATAA data)
{
  wchar_t wide[MAX_PATH * 2]{};

  if (!to_wide(name, wide)) {
    return orig_find_first_file_a(name, data);
  }

  const wchar_t* target = redirect(wide, 0);

  if (target == wide) {
    return orig_find_first_file_a(name, data);
  }

  WIN32_FIND_DATAW found{};
  HANDLE handle = orig_find_first_file_w(target, &found);

  if (handle != INVALID_HANDLE_VALUE) {
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

  return handle;
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

void
install_file_hooks(const std::filesystem::path& root, file_overrides files)
{
  if (files.empty()) {
    log_line(L"Hooks: skipped, no mod files");
    return;
  }

  game_root = lower(root.native()) + L'\\';
  overrides = std::move(files);

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
  };

  MH_STATUS status = MH_Initialize();

  if (status != MH_OK) {
    log_line(L"Hooks: {}", widen(MH_StatusToString(status)));
    return;
  }

  const size_t created = create_api_hooks(hooks);
  status = MH_EnableHook(MH_ALL_HOOKS);

  if (status != MH_OK) {
    log_line(L"Hooks: {}", widen(MH_StatusToString(status)));
    return;
  }

  log_line(L"Hooks: {}/{}", created, std::size(hooks));
}
