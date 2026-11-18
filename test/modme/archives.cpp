#include "archive.h"
#include "game.h"

#include <array>
#include <atomic>
#include <doctest.h>
#include <limits>
#include <thread>
#include <unordered_set>

struct dir_entry
{
  uint32_t offset;
  uint32_t size;
  char name[24];
};

static const DWORD sector = 2048;

struct test_handle
{
  HANDLE value;
  ~test_handle()
  {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
  test_handle(const test_handle&) = delete;
  explicit test_handle(HANDLE handle)
    : value(handle)
  {
  }
};

static void
check_no_completion(HANDLE port)
{
  DWORD done = 0;
  ULONG_PTR key = 0;
  OVERLAPPED* completed = nullptr;
  CHECK_FALSE(GetQueuedCompletionStatus(port, &done, &key, &completed, 0));
  CHECK(GetLastError() == WAIT_TIMEOUT);
  CHECK(completed == nullptr);
}

static HANDLE
open_archive(const wchar_t* path,
             DWORD flags = 0,
             DWORD share = FILE_SHARE_READ)
{
  return CreateFileW(
    path, GENERIC_READ, share, nullptr, OPEN_EXISTING, flags, nullptr);
}

static std::string
read_range(HANDLE file, DWORD offset, DWORD length)
{
  std::string data(length, '\0');
  DWORD done = 0;

  SetFilePointer(file, LONG(offset), nullptr, FILE_BEGIN);
  ReadFile(file, data.data(), length, &done, nullptr);
  data.resize(done);

  return data;
}

static std::string
text(const std::string& data)
{
  return data.substr(0, data.find('\0'));
}

static std::vector<dir_entry>
read_dir()
{
  HANDLE file = open_archive(L"Stream\\World.dir");
  const std::string data = read_range(file, 0, GetFileSize(file, nullptr));
  std::vector<dir_entry> entries(data.size() / sizeof(dir_entry));

  CloseHandle(file);
  std::memcpy(entries.data(), data.data(), entries.size() * sizeof(dir_entry));

  return entries;
}

static std::vector<dir_entry>
named(const char* name)
{
  std::vector<dir_entry> found;

  for (const dir_entry& entry : read_dir()) {
    if (strncmp(entry.name, name, sizeof(entry.name)) == 0) {
      found.push_back(entry);
    }
  }

  return found;
}

static BOOL
read_overlapped(HANDLE file, DWORD offset, std::string& data)
{
  OVERLAPPED overlapped{};
  DWORD done = 0;

  overlapped.Offset = offset;
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

  BOOL ok =
    ReadFile(file, data.data(), DWORD(data.size()), nullptr, &overlapped);

  if (ok || GetLastError() == ERROR_IO_PENDING) {
    ok = GetOverlappedResult(file, &overlapped, &done, TRUE);
  }

  const DWORD error = GetLastError();

  CloseHandle(overlapped.hEvent);
  data.resize(ok ? done : 0);
  SetLastError(error);

  return ok;
}

TEST_CASE("mod entries that fit replace archive entries in place")
{
  REQUIRE(game().modme != nullptr);

  const std::vector<dir_entry> shrunk = named("small.txt");
  REQUIRE(shrunk.size() == 1);
  CHECK(shrunk[0].offset == 1);
  CHECK(shrunk[0].size == 1);

  HANDLE img = open_archive(L"Stream\\World.img");
  REQUIRE(img != INVALID_HANDLE_VALUE);

  CHECK(read_range(img, 0, 4) == "keep");
  CHECK(read_range(img, sector, sector) ==
        std::string("new small") + std::string(sector - 9, '\0'));
  CHECK(read_range(img, 2 * sector, sector) == std::string(sector, '\0'));
  CloseHandle(img);
}

TEST_CASE("larger mod entries move to the end of the archive")
{
  REQUIRE(game().modme != nullptr);

  const std::vector<dir_entry> grow = named("grow.txt");
  REQUIRE(grow.size() == 1);
  CHECK(grow[0].offset == 6);
  CHECK(grow[0].size == 2);

  HANDLE img = open_archive(L"Stream\\World.img");
  REQUIRE(img != INVALID_HANDLE_VALUE);

  LARGE_INTEGER size{};
  CHECK(GetFileSize(img, nullptr) == 9 * sector);
  CHECK(GetFileSizeEx(img, &size));
  CHECK(size.QuadPart == 9 * sector);
  CHECK(SetFilePointer(img, 0, nullptr, FILE_END) == 9 * sector);
  CHECK(read_range(img, 6 * sector, 3000) == std::string(3000, 'g'));
  CloseHandle(img);
}

TEST_CASE("every entry with the same name gets the mod file")
{
  REQUIRE(game().modme != nullptr);

  const std::vector<dir_entry> twice = named("twice.txt");
  REQUIRE(twice.size() == 2);

  HANDLE img = open_archive(L"Stream\\World.img");
  REQUIRE(img != INVALID_HANDLE_VALUE);

  for (const dir_entry& entry : twice) {
    CHECK(entry.size == 1);
    CHECK(text(read_range(img, entry.offset * sector, sector)) == "new twice");
  }

  CloseHandle(img);
}

TEST_CASE("new mod entries are added to the archive")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  const std::vector<dir_entry> added = named("added.txt");
  REQUIRE(added.size() == 1);
  CHECK(added[0].offset == 8);
  CHECK(read_dir().size() == 6);

  HANDLE img = open_archive(L"Stream\\World.img");
  CHECK(text(read_range(img, added[0].offset * sector, sector)) == "added");
  CloseHandle(img);

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(log.find("Skipped Stream\\World.img\\this_name_is_longer_than_24.txt "
                 "from TestMod, entry names are ASCII and up to 24 "
                 "characters") != std::string::npos);
}

TEST_CASE("empty mod entries remove archive entries")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  CHECK(named("gone.txt").empty());
  CHECK(named("nothing.txt").empty());

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(log.find("Removed Stream\\World.img\\gone.txt by TestMod") !=
        std::string::npos);
  CHECK(log.find("Skipped Stream\\World.img\\nothing.txt from TestMod, empty "
                 "and not in the archive") != std::string::npos);
}

TEST_CASE("overlapped reads mix game and mod data")
{
  REQUIRE(game().modme != nullptr);

  HANDLE img = open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED);
  REQUIRE(img != INVALID_HANDLE_VALUE);

  std::string game_only(sector, '\0');
  REQUIRE(read_overlapped(img, 0, game_only));
  CHECK(text(game_only) == "keep");

  std::string mixed(2 * sector, '\0');
  REQUIRE(read_overlapped(img, 0, mixed));
  CHECK(text(mixed) == "keep");
  CHECK(text(mixed.substr(sector)) == "new small");

  std::string past_end(sector, '\0');
  CHECK_FALSE(read_overlapped(img, 9 * sector, past_end));
  CHECK(GetLastError() == ERROR_HANDLE_EOF);
  CloseHandle(img);
}

TEST_CASE("whole archive reads and unshared opens see mod entries")
{
  REQUIRE(game().modme != nullptr);

  HANDLE img = open_archive(L"Stream\\World.img", 0, 0);
  REQUIRE(img != INVALID_HANDLE_VALUE);

  const std::string data = read_range(img, 0, 16 * sector);
  CloseHandle(img);

  REQUIRE(data.size() == 9 * sector);
  CHECK(text(data) == "keep");
  CHECK(text(data.substr(4 * sector)) == "new twice");
  CHECK(data[6 * sector] == 'g');
}

TEST_CASE("archive files on disk and in listings stay the game's")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  CHECK(fs::file_size(g.root / "Stream" / "World.img") == 6 * sector);
  CHECK(fs::is_regular_file(g.root / "Stream" / "World.img"));

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(log.find("Replaced Stream\\World.img\\Small.TXT from TestMod") !=
        std::string::npos);
  CHECK(log.find("Conflict: Stream\\World.img\\small.txt from Zażółć mod "
                 "ignored, using TestMod") != std::string::npos);
}

TEST_CASE(
  "a reused handle of a file closed without CloseHandle is not an archive")
{
  REQUIRE(game().modme != nullptr);

  using nt_close_t = LONG(WINAPI*)(HANDLE);
  const auto nt_close = reinterpret_cast<nt_close_t>(
    GetProcAddress(GetModuleHandleW(L"ntdll"), "NtClose"));
  REQUIRE(nt_close != nullptr);

  HANDLE img = open_archive(L"Stream\\World.img");
  REQUIRE(img != INVALID_HANDLE_VALUE);
  nt_close(img);

  HANDLE other = open_archive(L"TXD\\other.nft");
  REQUIRE(other == img);
  CHECK(read(other) == "untouched");
}

TEST_CASE("archives never grow past 4 GB")
{
  const fs::path file = exe_dir() / "scan" / "limit" / "new.txt";
  fs::create_directories(file.parent_path());
  std::ofstream(file) << "new";

  file_overrides entries;
  entries.emplace(
    L"new.txt",
    mod_file{ file.native(), L"a_mod", L"Stream\\World.img\\new.txt" });

  const dir_entry last{ 2097150, 1, "last.txt" };
  const std::string dir(reinterpret_cast<const char*>(&last), sizeof(last));
  const virtual_archive archive = build_archive(dir, entries);

  CHECK(archive.dir == dir);
  CHECK(archive.size == 4294965248u);
}

TEST_CASE("virtual archive reads deliver exactly one IOCP completion")
{
  REQUIRE(game().modme != nullptr);
  const test_handle img(
    open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED));
  REQUIRE(img.value != INVALID_HANDLE_VALUE);
  const ULONG_PTR expected_key = 1234;
  const test_handle port(
    CreateIoCompletionPort(img.value, nullptr, expected_key, 0));
  REQUIRE(port.value != nullptr);
  const test_handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  REQUIRE(event.value != nullptr);

  // The mixed read uses the original archive internally. Those private reads
  // must not enqueue their stack-allocated OVERLAPPED structures to this port.
  for (DWORD offset : { DWORD(0), sector, 6 * sector, 8 * sector }) {
    std::string data(2 * sector, '\0');
    OVERLAPPED request{};
    request.Offset = offset;
    request.hEvent = event.value;
    const BOOL ok =
      ReadFile(img.value, data.data(), DWORD(data.size()), nullptr, &request);
    REQUIRE((ok || GetLastError() == ERROR_IO_PENDING));
    DWORD done = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* completed = nullptr;
    const BOOL notified =
      GetQueuedCompletionStatus(port.value, &done, &key, &completed, 1000);
    CHECK(notified);
    CHECK(completed == &request);
    CHECK(key == expected_key);
    CHECK(done == std::min<DWORD>(2 * sector, 9 * sector - offset));
    CHECK(WaitForSingleObject(event.value, 0) == WAIT_OBJECT_0);
    if (offset == sector)
      CHECK(text(data) == "new small");
    check_no_completion(port.value);
  }
}

TEST_CASE("archive completion suppression still signals the explicit event")
{
  REQUIRE(game().modme != nullptr);
  const test_handle img(
    open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED));
  REQUIRE(img.value != INVALID_HANDLE_VALUE);
  const test_handle port(CreateIoCompletionPort(img.value, nullptr, 42, 0));
  REQUIRE(port.value != nullptr);
  const test_handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  REQUIRE(event.value != nullptr);

  for (bool skip_success : { false, true }) {
    if (skip_success) {
      REQUIRE(SetFileCompletionNotificationModes(
        img.value,
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE));
      // Notification modes cannot be removed by a later call with zero flags.
      REQUIRE(SetFileCompletionNotificationModes(img.value, 0));
    }
    OVERLAPPED request{};
    request.hEvent = skip_success
                       ? event.value
                       : reinterpret_cast<HANDLE>(
                           reinterpret_cast<ULONG_PTR>(event.value) | 1);
    std::string data(2 * sector, '\0');
    REQUIRE(ResetEvent(event.value));
    REQUIRE(
      ReadFile(img.value, data.data(), DWORD(data.size()), nullptr, &request));
    CHECK(WaitForSingleObject(event.value, 0) == WAIT_OBJECT_0);
    DWORD done = 0;
    REQUIRE(GetOverlappedResult(img.value, &request, &done, FALSE));
    CHECK(done == 2 * sector);
    CHECK(text(data) == "keep");
    CHECK(text(data.substr(sector)) == "new small");
    check_no_completion(port.value);
  }
}

TEST_CASE("archive IOCP associations preserve the first key and port lifetime")
{
  REQUIRE(game().modme != nullptr);
  const test_handle img(
    open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED));
  REQUIRE(img.value != INVALID_HANDLE_VALUE);
  HANDLE original = CreateIoCompletionPort(img.value, nullptr, 17, 0);
  REQUIRE(original != nullptr);
  CHECK(CreateIoCompletionPort(img.value, original, 99, 0) == nullptr);
  HANDLE duplicate = nullptr;
  const BOOL copied = DuplicateHandle(GetCurrentProcess(),
                                      original,
                                      GetCurrentProcess(),
                                      &duplicate,
                                      0,
                                      FALSE,
                                      DUPLICATE_SAME_ACCESS);
  const test_handle port(duplicate);
  CloseHandle(original);
  REQUIRE(copied);
  OVERLAPPED request{};
  request.Offset = sector;
  std::array<char, 16> buffer{};
  REQUIRE(ReadFile(
    img.value, buffer.data(), DWORD(buffer.size()), nullptr, &request));
  DWORD done = 0;
  ULONG_PTR key = 0;
  OVERLAPPED* completed = nullptr;
  REQUIRE(GetQueuedCompletionStatus(port.value, &done, &key, &completed, 1000));
  CHECK(key == 17);
  CHECK(completed == &request);
  CHECK(done == buffer.size());
  check_no_completion(port.value);
}

static std::string
expected_world()
{
  // Independent byte layout for the fixture, including sector padding and the
  // original location of the entry moved to the end of the virtual archive.
  std::string expected(9 * sector, '\0');
  expected.replace(0, 4, "keep");
  expected.replace(sector, 9, "new small");
  expected.replace(3 * sector, 8, "old grow");
  expected.replace(4 * sector, 9, "new twice");
  expected.replace(5 * sector, 9, "new twice");
  expected.replace(6 * sector, 3000, std::string(3000, 'g'));
  expected.replace(8 * sector, 5, "added");
  return expected;
}

TEST_CASE(
  "virtual archive bytes match the expected image across read boundaries")
{
  REQUIRE(game().modme != nullptr);
  const std::string expected = expected_world();
  const test_handle sync(open_archive(L"Stream\\World.img"));
  const test_handle async(
    open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED));
  REQUIRE(sync.value != INVALID_HANDLE_VALUE);
  REQUIRE(async.value != INVALID_HANDLE_VALUE);
  CHECK(read_range(sync.value, 0, DWORD(expected.size() + sector)) == expected);

  for (DWORD offset : { DWORD(0),
                        sector - 7,
                        sector + 3,
                        3 * sector - 1,
                        6 * sector - 7,
                        8 * sector - 7,
                        9 * sector - 3 }) {
    for (DWORD length : { DWORD(1), DWORD(19), sector + 13, 4 * sector }) {
      CAPTURE(offset);
      CAPTURE(length);
      const std::string part = expected.substr(offset, length);
      CHECK(read_range(sync.value, offset, length) == part);
      std::string result(length, 'x');
      REQUIRE(read_overlapped(async.value, offset, result));
      CHECK(result == part);
    }
  }
  CHECK(read_range(sync.value, DWORD(expected.size()), 10).empty());
  std::string past_end(10, 'x');
  CHECK_FALSE(read_overlapped(async.value, DWORD(expected.size()), past_end));
  CHECK(GetLastError() == ERROR_HANDLE_EOF);
}

TEST_CASE("parallel archive reads keep separate offsets and replacement data")
{
  REQUIRE(game().modme != nullptr);
  const std::string expected = expected_world();
  const test_handle shared(
    open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED));
  REQUIRE(shared.value != INVALID_HANDLE_VALUE);
  std::atomic<bool> correct = true;
  std::vector<std::thread> readers;
  for (DWORD worker = 0; worker != 6; ++worker) {
    readers.emplace_back([&, worker] {
      const test_handle sync(open_archive(L"Stream\\World.img"));
      if (sync.value == INVALID_HANDLE_VALUE) {
        correct = false;
        return;
      }
      for (DWORD i = 0; i != 100; ++i) {
        const DWORD offset = (worker * 997 + i * 193) % DWORD(expected.size());
        const DWORD length = sector + 37;
        const auto part = expected.substr(offset, length);
        if (read_range(sync.value, offset, length) != part)
          correct = false;
        std::string result(length, 'x');
        if (!read_overlapped(shared.value, offset, result) || result != part) {
          correct = false;
        }
      }
    });
  }
  for (auto& reader : readers)
    reader.join();
  CHECK(correct.load());
}

TEST_CASE(
  "closing an archive during emulated reads does not invalidate its state")
{
  REQUIRE(game().modme != nullptr);
  std::atomic<bool> correct = true;
  for (int attempt = 0; attempt != 50; ++attempt) {
    const HANDLE img = open_archive(L"Stream\\World.img", FILE_FLAG_OVERLAPPED);
    REQUIRE(img != INVALID_HANDLE_VALUE);
    std::atomic<bool> started = false;
    std::thread reader([&] {
      started = true;
      for (int i = 0; i != 50; ++i) {
        OVERLAPPED request{};
        request.Offset = sector;
        char buffer[9]{};
        DWORD done = 0;
        const BOOL ok = ReadFile(img, buffer, sizeof(buffer), &done, &request);
        if (ok) {
          if (done != sizeof(buffer) ||
              std::string(buffer, done) != "new small")
            correct = false;
        } else if (GetLastError() != ERROR_INVALID_HANDLE) {
          correct = false;
        }
      }
    });
    while (!started.load())
      std::this_thread::yield();
    const BOOL closed = CloseHandle(img);
    reader.join();
    CHECK(closed);
  }
  CHECK(correct.load());
}

static std::string
binary_contents(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(in), {} };
}

TEST_CASE(
  "explicit offsets on synchronous archives update the virtual position")
{
  REQUIRE(game().modme != nullptr);
  const test_handle image(open_archive(L"Stream\\World.img"));
  REQUIRE(image.value != INVALID_HANDLE_VALUE);
  OVERLAPPED request{};
  request.Offset = 1;
  char buffer[3]{};
  DWORD done = 0;
  REQUIRE(ReadFile(image.value, buffer, sizeof(buffer), &done, &request));
  CHECK(std::string(buffer, done) == "eep");
  CHECK(SetFilePointer(image.value, 0, nullptr, FILE_CURRENT) == 4);

  request = {};
  request.Offset = 9 * sector;
  done = 123;
  CHECK(ReadFile(image.value, buffer, sizeof(buffer), &done, &request));
  CHECK(done == 0);
}

TEST_CASE("virtual archive seeks reject overflow and retain their position")
{
  REQUIRE(game().modme != nullptr);
  const test_handle image(open_archive(L"Stream\\World.img"));
  REQUIRE(image.value != INVALID_HANDLE_VALUE);
  LARGE_INTEGER distance{};
  distance.QuadPart = std::numeric_limits<int64_t>::max();
  REQUIRE(SetFilePointerEx(image.value, distance, nullptr, FILE_BEGIN));
  distance.QuadPart = 1;
  CHECK_FALSE(SetFilePointerEx(image.value, distance, nullptr, FILE_CURRENT));
  CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
  distance.QuadPart = 0;
  LARGE_INTEGER position{};
  REQUIRE(SetFilePointerEx(image.value, distance, &position, FILE_CURRENT));
  CHECK(position.QuadPart == std::numeric_limits<int64_t>::max());
  distance.QuadPart = std::numeric_limits<int64_t>::min();
  CHECK_FALSE(SetFilePointerEx(image.value, distance, nullptr, FILE_CURRENT));
  CHECK(GetLastError() == ERROR_NEGATIVE_SEEK);
}

TEST_CASE("real IMG entries match the original archive and a supplied mod")
{
  const fs::path source = test_environment_path(L"MODME_TEST_ARCHIVE");
  const fs::path replacements = test_environment_path(L"MODME_TEST_ENTRIES");
  if (source.empty() && replacements.empty()) {
    MESSAGE("Set MODME_TEST_ARCHIVE and MODME_TEST_ENTRIES to run the real "
            "archive test");
    return;
  }
  REQUIRE_FALSE(source.empty());
  REQUIRE_FALSE(replacements.empty());
  REQUIRE(game().modme != nullptr);
  const auto original_dir =
    binary_contents(fs::path(source).replace_extension(".dir"));
  REQUIRE(original_dir.size() % sizeof(dir_entry) == 0);
  std::vector<dir_entry> originals(original_dir.size() / sizeof(dir_entry));
  std::memcpy(originals.data(), original_dir.data(), original_dir.size());

  const test_handle dir(open_archive(L"Stream\\Real.dir"));
  REQUIRE(dir.value != INVALID_HANDLE_VALUE);
  const auto dir_bytes =
    read_range(dir.value, 0, GetFileSize(dir.value, nullptr));
  REQUIRE(dir_bytes.size() % sizeof(dir_entry) == 0);
  std::vector<dir_entry> entries(dir_bytes.size() / sizeof(dir_entry));
  std::memcpy(entries.data(), dir_bytes.data(), dir_bytes.size());
  const test_handle image(open_archive(L"Stream\\Real.img"));
  const test_handle async(
    open_archive(L"Stream\\Real.img", FILE_FLAG_OVERLAPPED));
  REQUIRE(image.value != INVALID_HANDLE_VALUE);
  REQUIRE(async.value != INVALID_HANDLE_VALUE);
  const test_handle port(CreateIoCompletionPort(async.value, nullptr, 77, 0));
  REQUIRE(port.value != nullptr);

  std::unordered_map<std::wstring, fs::path> mods;
  for (const auto& file : fs::directory_iterator(replacements)) {
    if (file.is_regular_file())
      mods.emplace(lower(file.path().filename().native()), file.path());
  }
  std::ifstream original_image(source, std::ios::binary);
  REQUIRE(original_image.good());
  size_t index = 0;
  size_t replaced = 0;
  size_t untouched = 0;
  const auto check_entry = [&](const dir_entry& entry,
                               const std::string& expected,
                               bool modded) {
    const std::string name(entry.name, strnlen(entry.name, sizeof(entry.name)));
    CAPTURE(name);
    const auto bytes =
      read_range(image.value, entry.offset * sector, entry.size * sector);
    const bool equal = bytes == expected;
    REQUIRE(equal);
    if (modded && !expected.empty()) {
      std::string result(expected.size(), 'x');
      OVERLAPPED request{};
      request.Offset = entry.offset * sector;
      const BOOL ok = ReadFile(
        async.value, result.data(), DWORD(result.size()), nullptr, &request);
      REQUIRE((ok || GetLastError() == ERROR_IO_PENDING));
      DWORD done = 0;
      ULONG_PTR key = 0;
      OVERLAPPED* completed = nullptr;
      REQUIRE(
        GetQueuedCompletionStatus(port.value, &done, &key, &completed, 1000));
      REQUIRE(completed == &request);
      REQUIRE(key == 77);
      REQUIRE(done == expected.size());
      const bool async_equal = result == expected;
      REQUIRE(async_equal);
      check_no_completion(port.value);
    }
  };

  std::unordered_set<std::wstring> existing;
  for (const auto& original : originals) {
    const std::string name(original.name,
                           strnlen(original.name, sizeof(original.name)));
    const std::wstring key = lower(std::wstring(name.begin(), name.end()));
    existing.insert(key);
    const auto mod = mods.find(key);
    if (mod != mods.end() && fs::file_size(mod->second) == 0)
      continue;
    REQUIRE(index < entries.size());
    const auto& entry = entries[index++];
    REQUIRE(std::memcmp(original.name, entry.name, sizeof(entry.name)) == 0);
    std::string expected;
    if (mod != mods.end()) {
      expected = binary_contents(mod->second);
      ++replaced;
    } else {
      expected.resize(size_t(original.size) * sector);
      original_image.seekg(uint64_t(original.offset) * sector);
      original_image.read(expected.data(), std::streamsize(expected.size()));
      REQUIRE(original_image.good());
      ++untouched;
    }
    expected.resize(size_t(entry.size) * sector, '\0');
    check_entry(entry, expected, mod != mods.end());
  }
  while (index < entries.size()) {
    const auto& entry = entries[index++];
    const std::string name(entry.name, strnlen(entry.name, sizeof(entry.name)));
    const auto key = lower(std::wstring(name.begin(), name.end()));
    REQUIRE_FALSE(existing.contains(key));
    REQUIRE(mods.contains(key));
    auto expected = binary_contents(mods.at(key));
    REQUIRE_FALSE(expected.empty());
    expected.resize(size_t(entry.size) * sector, '\0');
    check_entry(entry, expected, true);
    ++replaced;
  }
  MESSAGE("Real archive: ",
          replaced,
          " replacement entries, ",
          untouched,
          " original entries checked");
  REQUIRE(replaced > 0);
  REQUIRE(untouched > 0);
}
