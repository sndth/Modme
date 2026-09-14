#include "archive.h"
#include "game.h"

#include <doctest.h>

struct dir_entry
{
  uint32_t offset;
  uint32_t size;
  char name[24];
};

static const DWORD sector = 2048;

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
