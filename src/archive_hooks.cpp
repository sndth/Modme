#include "archive_hooks.h"
#include "archive.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

struct archive_state
{
  ~archive_state();
  std::shared_ptr<const scanned_mods> mods;
  virtual_archive layout;
  std::mutex mutex;
  std::unordered_map<const mod_file*, HANDLE> files;
};

struct tracked_file
{
  archive_state* archive;
  bool dir;
  bool overlapped;
  std::mutex mutex;
  uint64_t position = 0;
  bool closed = false;
  std::shared_ptr<void> completion_port;
  ULONG_PTR completion_key = 0;
  UCHAR completion_modes = 0;
};

static const ULONG_PTR status_end_of_file = 0xC0000011;

static std::mutex archives_mutex;
static std::unordered_map<std::wstring, std::unique_ptr<archive_state>> built;
static std::unordered_set<std::wstring> failed;
static std::shared_mutex handles_mutex;
static std::unordered_map<HANDLE, std::shared_ptr<tracked_file>> handles;
static std::atomic<size_t> tracked;

static decltype(ReadFile)* orig_read_file;
static decltype(SetFilePointer)* orig_set_file_pointer;
static decltype(SetFilePointerEx)* orig_set_file_pointer_ex;
static decltype(GetFileSize)* orig_get_file_size;
static decltype(GetFileSizeEx)* orig_get_file_size_ex;
static decltype(CloseHandle)* orig_close_handle;
static decltype(CreateIoCompletionPort)* orig_create_io_completion_port;
static decltype(SetFileCompletionNotificationModes)* orig_set_completion_modes;

archive_state::~archive_state()
{
  for (const auto& [file, handle] : files) {
    if (handle != INVALID_HANDLE_VALUE) {
      orig_close_handle(handle);
    }
  }
}

static bool
read_at(HANDLE file, bool overlapped, uint64_t offset, char* out, DWORD length)
{
  OVERLAPPED position{};
  DWORD done = 0;

  position.Offset = DWORD(offset);
  position.OffsetHigh = DWORD(offset >> 32);
  const HANDLE event =
    overlapped ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;

  if (overlapped && !event) {
    return false;
  }

  // Private reads must never deliver their temporary OVERLAPPED to a game's
  // completion port. The low event bit suppresses those native packets.
  position.hEvent =
    event ? reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(event) | 1)
          : nullptr;

  BOOL ok = orig_read_file(file, out, length, &done, &position);

  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    ok = GetOverlappedResult(file, &position, &done, TRUE);
  }

  const DWORD error = ok ? NO_ERROR : GetLastError();

  if (event) {
    orig_close_handle(event);
  }

  SetLastError(error);

  return ok || error == ERROR_HANDLE_EOF;
}

static std::optional<std::string>
read_all(HANDLE file, bool overlapped)
{
  LARGE_INTEGER size{};

  if (!orig_get_file_size_ex(file, &size)) {
    return std::nullopt;
  }

  std::string contents(size_t(size.QuadPart), '\0');

  if (!read_at(file, overlapped, 0, contents.data(), DWORD(contents.size()))) {
    return std::nullopt;
  }

  return contents;
}

static archive_state*
find_archive(const std::wstring& img, HANDLE handle, bool dir, bool overlapped)
{
  std::lock_guard lock(archives_mutex);
  std::shared_ptr<const scanned_mods> mods = current_mods();

  if (auto it = built.find(img); it != built.end()) {
    return it->second.get();
  }

  auto entries = mods->archives.find(img);

  if (entries == mods->archives.end()) {
    return nullptr;
  }

  std::optional<std::string> contents;
  const std::wstring dir_file = img.substr(0, img.size() - 4) + L".dir";

  if (dir) {
    contents = read_all(handle, overlapped);
  } else if (HANDLE file = open_game_file(dir_file);
             file != INVALID_HANDLE_VALUE) {
    contents = read_all(file, false);
    orig_close_handle(file);
  }

  if (!contents) {
    const DWORD error = GetLastError();
    const std::wstring& entry = entries->second.begin()->second.name;
    const std::wstring name = entry.substr(0, entry.rfind(L'\\'));

    if (failed.insert(img).second) {
      log_line(L"Skipped {}, its .dir can't be read (error {})", name, error);
    }

    return nullptr;
  }

  auto archive = std::make_unique<archive_state>();

  archive->layout = build_archive(std::move(*contents), entries->second);
  archive->mods = std::move(mods);

  return built.emplace(img, std::move(archive)).first->second.get();
}

static std::shared_ptr<tracked_file>
find_tracked(HANDLE handle)
{
  if (tracked.load(std::memory_order_relaxed) == 0) {
    return nullptr;
  }

  std::shared_lock lock(handles_mutex);
  auto it = handles.find(handle);

  return it == handles.end() ? nullptr : it->second;
}

static uint64_t
size_of(const tracked_file& file)
{
  return file.dir ? file.archive->layout.dir.size() : file.archive->layout.size;
}

static HANDLE
entry_handle(archive_state& archive, const mod_file& file)
{
  std::lock_guard lock(archive.mutex);
  auto [it, added] = archive.files.try_emplace(&file, INVALID_HANDLE_VALUE);

  if (added) {
    it->second = open_for_reading(file.path.c_str());

    if (it->second != INVALID_HANDLE_VALUE) {
      log_line(L"Replaced {} from {}", file.name, file.mod);
    } else {
      log_line(
        L"Skipped {} from {}, error {}", file.name, file.mod, GetLastError());
    }
  }

  if (it->second == INVALID_HANDLE_VALUE) {
    SetLastError(ERROR_OPEN_FAILED);
  }

  return it->second;
}

static std::vector<archive_part>::const_iterator
part_at(const virtual_archive& layout, uint64_t offset)
{
  auto it =
    std::ranges::upper_bound(layout.parts, offset, {}, &archive_part::offset);

  return it == layout.parts.begin() ? layout.parts.end() : std::prev(it);
}

static bool
passes_through(const tracked_file& file, uint64_t offset, DWORD length)
{
  const virtual_archive& layout = file.archive->layout;
  auto it = part_at(layout, offset);

  if (file.dir || it == layout.parts.end() || it->file) {
    return false;
  }

  const uint64_t end = it->offset + it->size;

  return offset <= end && length <= end - offset;
}

static bool
read_archive(tracked_file& file,
             HANDLE handle,
             uint64_t offset,
             LPVOID buffer,
             DWORD& length)
{
  const virtual_archive& layout = file.archive->layout;
  const uint64_t size = size_of(file);
  char* out = static_cast<char*>(buffer);

  length = offset < size ? DWORD(std::min<uint64_t>(length, size - offset)) : 0;

  if (length == 0) {
    return true;
  }

  if (file.dir) {
    std::copy_n(layout.dir.data() + offset, length, out);
    return true;
  }

  std::fill_n(out, length, 0);

  const uint64_t end = offset + length;

  for (auto it = part_at(layout, offset);
       it != layout.parts.end() && it->offset < end;
       ++it) {
    const uint64_t from = std::max<uint64_t>(offset, it->offset);
    const uint64_t to = std::min<uint64_t>(end, it->offset + it->size);

    if (from >= to) {
      continue;
    }

    const bool ok = it->file ? read_at(entry_handle(*file.archive, *it->file),
                                       false,
                                       from - it->offset,
                                       out + (from - offset),
                                       DWORD(to - from))
                             : read_at(handle,
                                       file.overlapped,
                                       from,
                                       out + (from - offset),
                                       DWORD(to - from));

    if (!ok) {
      return false;
    }
  }

  return true;
}

static std::optional<uint64_t>
seek(tracked_file& file, int64_t distance, DWORD method)
{
  std::lock_guard lock(file.mutex);
  if (file.closed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return std::nullopt;
  }
  int64_t from = 0;

  if (method == FILE_CURRENT) {
    from = int64_t(file.position);
  } else if (method == FILE_END) {
    from = int64_t(size_of(file));
  } else if (method != FILE_BEGIN) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return std::nullopt;
  }

  if (distance < -from) {
    SetLastError(ERROR_NEGATIVE_SEEK);
    return std::nullopt;
  }

  if (distance > std::numeric_limits<int64_t>::max() - from) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return std::nullopt;
  }

  file.position = uint64_t(from + distance);
  SetLastError(NO_ERROR);

  return file.position;
}

static BOOL WINAPI
hook_read_file(HANDLE handle,
               LPVOID buffer,
               DWORD length,
               LPDWORD read,
               LPOVERLAPPED overlapped)
{
  const auto file = find_tracked(handle);
  const uint64_t offset =
    overlapped ? (uint64_t(overlapped->OffsetHigh) << 32) | overlapped->Offset
               : 0;

  if (!file) {
    return orig_read_file(handle, buffer, length, read, overlapped);
  }

  std::lock_guard lock(file->mutex);
  if (read) {
    *read = 0;
  }
  if (file->closed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if ((length && !buffer) || (file->overlapped && !overlapped) ||
      (!overlapped && !read)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (file->overlapped && overlapped && passes_through(*file, offset, length)) {
    return orig_read_file(handle, buffer, length, read, overlapped);
  }

  if (!overlapped) {
    if (!read_archive(*file, handle, file->position, buffer, length)) {
      return FALSE;
    }

    file->position += length;

    if (read) {
      *read = length;
    }

    return TRUE;
  }

  if (!read_archive(*file, handle, offset, buffer, length)) {
    return FALSE;
  }

  overlapped->InternalHigh = length;

  if (file->overlapped && length == 0 && offset >= size_of(*file)) {
    overlapped->Internal = status_end_of_file;
    SetLastError(ERROR_HANDLE_EOF);
    return FALSE;
  }

  overlapped->Internal = 0;

  if (!file->overlapped) {
    file->position = offset + length;
  }

  if (read) {
    *read = length;
  }

  const ULONG_PTR event = reinterpret_cast<ULONG_PTR>(overlapped->hEvent);
  if (const HANDLE handle = reinterpret_cast<HANDLE>(event & ~ULONG_PTR(1));
      handle && !SetEvent(handle)) {
    return FALSE;
  }

  if (file->overlapped && file->completion_port && !(event & 1) &&
      !(file->completion_modes & FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
    if (!PostQueuedCompletionStatus(file->completion_port.get(),
                                    length,
                                    file->completion_key,
                                    overlapped)) {
      return FALSE;
    }
  }

  return TRUE;
}

static DWORD WINAPI
hook_set_file_pointer(HANDLE handle, LONG distance, PLONG high, DWORD method)
{
  const auto file = find_tracked(handle);

  if (!file) {
    return orig_set_file_pointer(handle, distance, high, method);
  }

  int64_t move = distance;

  if (high) {
    move = int64_t((uint64_t(DWORD(*high)) << 32) | DWORD(distance));
  } else if (method == FILE_BEGIN) {
    move = DWORD(distance);
  }

  const std::optional<uint64_t> position = seek(*file, move, method);

  if (!position) {
    return INVALID_SET_FILE_POINTER;
  }

  if (high) {
    *high = LONG(*position >> 32);
  }

  return DWORD(*position);
}

static BOOL WINAPI
hook_set_file_pointer_ex(HANDLE handle,
                         LARGE_INTEGER distance,
                         PLARGE_INTEGER moved,
                         DWORD method)
{
  const auto file = find_tracked(handle);

  if (!file) {
    return orig_set_file_pointer_ex(handle, distance, moved, method);
  }

  const std::optional<uint64_t> position =
    seek(*file, distance.QuadPart, method);

  if (!position) {
    return FALSE;
  }

  if (moved) {
    moved->QuadPart = LONGLONG(*position);
  }

  return TRUE;
}

static DWORD WINAPI
hook_get_file_size(HANDLE handle, LPDWORD high)
{
  const auto file = find_tracked(handle);

  if (!file) {
    return orig_get_file_size(handle, high);
  }

  std::lock_guard lock(file->mutex);
  if (file->closed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return INVALID_FILE_SIZE;
  }
  const uint64_t size = size_of(*file);

  if (high) {
    *high = DWORD(size >> 32);
  }

  SetLastError(NO_ERROR);

  return DWORD(size);
}

static BOOL WINAPI
hook_get_file_size_ex(HANDLE handle, PLARGE_INTEGER size)
{
  const auto file = find_tracked(handle);

  if (!file) {
    return orig_get_file_size_ex(handle, size);
  }

  std::lock_guard lock(file->mutex);
  if (file->closed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  size->QuadPart = LONGLONG(size_of(*file));

  return TRUE;
}

static BOOL WINAPI
hook_close_handle(HANDLE handle)
{
  if (const auto file = find_tracked(handle)) {
    std::lock_guard file_lock(file->mutex);
    if (file->closed) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    if (!orig_close_handle(handle)) {
      return FALSE;
    }
    const DWORD error = GetLastError();
    file->closed = true;
    std::unique_lock lock(handles_mutex);
    const auto it = handles.find(handle);
    if (it != handles.end() && it->second == file) {
      handles.erase(it);
      --tracked;
    }
    SetLastError(error);
    return TRUE;
  }

  return orig_close_handle(handle);
}

static HANDLE WINAPI
hook_create_io_completion_port(HANDLE handle,
                               HANDLE port,
                               ULONG_PTR key,
                               DWORD threads)
{
  const auto file = find_tracked(handle);
  if (!file) {
    return orig_create_io_completion_port(handle, port, key, threads);
  }
  std::lock_guard lock(file->mutex);
  if (file->closed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  const HANDLE result =
    orig_create_io_completion_port(handle, port, key, threads);
  const DWORD error = GetLastError();
  if (result && !file->completion_port) {
    HANDLE owned = nullptr;
    if (DuplicateHandle(GetCurrentProcess(),
                        result,
                        GetCurrentProcess(),
                        &owned,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS)) {
      file->completion_port = std::shared_ptr<void>(
        owned, [](HANDLE port) { orig_close_handle(port); });
      file->completion_key = key;
    } else {
      log_line(L"Archive: can't retain completion port (error {})",
               GetLastError());
    }
  }
  SetLastError(error);
  return result;
}

static BOOL WINAPI
hook_set_completion_modes(HANDLE handle, UCHAR modes)
{
  const auto file = find_tracked(handle);
  if (!file) {
    return orig_set_completion_modes(handle, modes);
  }
  std::lock_guard lock(file->mutex);
  if (file->closed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  const BOOL result = orig_set_completion_modes(handle, modes);
  if (result) {
    file->completion_modes |= modes;
  }
  return result;
}

void
log_archive_changes(const scanned_mods& mods)
{
  std::lock_guard lock(archives_mutex);

  for (const auto& [img, archive] : built) {
    auto after = mods.archives.find(img);

    if (after == mods.archives.end() ||
        after->second != archive->mods->archives.at(img)) {
      log_line(L"Hot reload: {} entries change after a restart", img);
    }
  }
}

static std::shared_ptr<tracked_file>
open_tracked(HANDLE handle, const wchar_t* name, DWORD flags)
{
  const std::wstring_view view = name ? name : L"";

  if (view.size() < 4) {
    return nullptr;
  }

  const std::wstring extension =
    lower(std::wstring(view.substr(view.size() - 4)));
  const bool dir = extension == L".dir";

  if (!dir && extension != L".img") {
    return nullptr;
  }

  std::wstring img = game_file(name);

  if (!img.ends_with(extension)) {
    return nullptr;
  }

  img.replace(img.size() - 4, 4, L".img");

  const bool overlapped = (flags & FILE_FLAG_OVERLAPPED) != 0;
  archive_state* archive = find_archive(img, handle, dir, overlapped);

  return archive ? std::make_shared<tracked_file>(archive, dir, overlapped)
                 : nullptr;
}

void
track_archive(HANDLE handle, const wchar_t* name, DWORD flags)
{
  auto file = open_tracked(handle, name, flags);

  if (!file && tracked.load(std::memory_order_relaxed) == 0) {
    return;
  }

  std::unique_lock lock(handles_mutex);

  if (!file) {
    tracked -= handles.erase(handle);
  } else if (handles.insert_or_assign(handle, std::move(file)).second) {
    ++tracked;
  }
}

std::span<const api_hook>
archive_hooks()
{
  static const api_hook hooks[] = {
    { "ReadFile", &hook_read_file, reinterpret_cast<void**>(&orig_read_file) },
    { "SetFilePointer",
      &hook_set_file_pointer,
      reinterpret_cast<void**>(&orig_set_file_pointer) },
    { "SetFilePointerEx",
      &hook_set_file_pointer_ex,
      reinterpret_cast<void**>(&orig_set_file_pointer_ex) },
    { "GetFileSize",
      &hook_get_file_size,
      reinterpret_cast<void**>(&orig_get_file_size) },
    { "GetFileSizeEx",
      &hook_get_file_size_ex,
      reinterpret_cast<void**>(&orig_get_file_size_ex) },
    { "CloseHandle",
      &hook_close_handle,
      reinterpret_cast<void**>(&orig_close_handle) },
    { "CreateIoCompletionPort",
      &hook_create_io_completion_port,
      reinterpret_cast<void**>(&orig_create_io_completion_port) },
    { "SetFileCompletionNotificationModes",
      &hook_set_completion_modes,
      reinterpret_cast<void**>(&orig_set_completion_modes) },
  };

  return hooks;
}
