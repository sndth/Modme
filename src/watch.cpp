#include "watch.h"
#include "log.h"
#include "mods.h"

#include <Windows.h>
#include <future>
#include <thread>

namespace fs = std::filesystem;

static const DWORD quiet_ms = 500;
static const DWORD changes =
  FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
  FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

static bool
mentions(const DWORD* buffer, const std::wstring& name)
{
  const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer);

  while (true) {
    const std::wstring changed(info->FileName,
                               info->FileNameLength / sizeof(wchar_t));

    if (lower(changed) == name) {
      return true;
    }

    if (info->NextEntryOffset == 0) {
      return false;
    }

    info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
      reinterpret_cast<const BYTE*>(info) + info->NextEntryOffset);
  }
}

static void
watch_folder(fs::path folder,
             bool subtree,
             std::wstring name,
             HANDLE changed,
             std::promise<void> ready)
{
  HANDLE handle =
    CreateFileW(folder.c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                nullptr);

  if (handle == INVALID_HANDLE_VALUE) {
    log_line(L"Hot reload: can't watch {} (error {})",
             folder.native(),
             GetLastError());
    ready.set_value();
    return;
  }

  const HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!completed) {
    log_line(L"Hot reload: can't create watch event (error {})",
             GetLastError());
    CloseHandle(handle);
    ready.set_value();
    return;
  }
  OVERLAPPED pending{};
  pending.hEvent = completed;
  DWORD buffer[16384]{};
  DWORD size = 0;
  bool first = true;

  while (true) {
    ResetEvent(completed);
    const BOOL queued = ReadDirectoryChangesW(handle,
                                              buffer,
                                              sizeof(buffer),
                                              subtree,
                                              changes,
                                              nullptr,
                                              &pending,
                                              nullptr);
    // Initialization must not return before Windows has armed the watch.
    if (first) {
      ready.set_value();
      first = false;
    }
    if (!queued || !GetOverlappedResult(handle, &pending, &size, TRUE)) {
      log_line(L"Hot reload: watch failed for {} (error {})",
               folder.native(),
               GetLastError());
      break;
    }
    if (size == 0 || name.empty() || mentions(buffer, name)) {
      SetEvent(changed);
    }
  }

  CloseHandle(completed);
  CloseHandle(handle);
}

void
watch_mods(const fs::path& mods,
           const fs::path& config,
           std::function<void()> reload)
{
  const HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!changed) {
    log_line(L"Hot reload: can't create change event (error {})",
             GetLastError());
    return;
  }

  std::promise<void> mods_ready;
  std::promise<void> config_ready;
  auto mods_started = mods_ready.get_future();
  auto config_started = config_ready.get_future();
  std::thread(
    watch_folder, mods, true, std::wstring(), changed, std::move(mods_ready))
    .detach();
  std::thread(watch_folder,
              config.parent_path(),
              false,
              lower(config.filename().native()),
              changed,
              std::move(config_ready))
    .detach();
  std::thread([changed, reload = std::move(reload)] {
    while (WaitForSingleObject(changed, INFINITE) == WAIT_OBJECT_0) {
      while (WaitForSingleObject(changed, quiet_ms) == WAIT_OBJECT_0) {
      }

      reload();
    }
  }).detach();
  mods_started.wait();
  config_started.wait();
}
