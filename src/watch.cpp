#include "watch.h"
#include "log.h"
#include "mods.h"

#include <Windows.h>
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
watch_folder(fs::path folder, bool subtree, std::wstring name, HANDLE changed)
{
  HANDLE handle =
    CreateFileW(folder.c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr);

  if (handle == INVALID_HANDLE_VALUE) {
    log_line(L"Hot reload: can't watch {} (error {})",
             folder.native(),
             GetLastError());
    return;
  }

  DWORD buffer[16384]{};
  DWORD size = 0;

  while (ReadDirectoryChangesW(handle,
                               buffer,
                               sizeof(buffer),
                               subtree,
                               changes,
                               &size,
                               nullptr,
                               nullptr)) {
    if (size == 0 || name.empty() || mentions(buffer, name)) {
      SetEvent(changed);
    }
  }

  CloseHandle(handle);
}

void
watch_mods(const fs::path& mods,
           const fs::path& config,
           std::function<void()> reload)
{
  const HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);

  std::thread(watch_folder, mods, true, std::wstring(), changed).detach();
  std::thread(watch_folder,
              config.parent_path(),
              false,
              lower(config.filename().native()),
              changed)
    .detach();
  std::thread([changed, reload = std::move(reload)] {
    while (WaitForSingleObject(changed, INFINITE) == WAIT_OBJECT_0) {
      while (WaitForSingleObject(changed, quiet_ms) == WAIT_OBJECT_0) {
      }

      reload();
    }
  }).detach();
}
