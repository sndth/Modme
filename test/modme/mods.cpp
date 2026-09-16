#include "mods.h"
#include "game.h"

#include <aclapi.h>
#include <doctest.h>

static bool
set_empty_dacl(const fs::path& folder, SECURITY_INFORMATION protection)
{
  std::wstring name = folder.native();
  ACL empty{};
  InitializeAcl(&empty, sizeof(empty), ACL_REVISION);

  return SetNamedSecurityInfoW(name.data(),
                               SE_FILE_OBJECT,
                               DACL_SECURITY_INFORMATION | protection,
                               nullptr,
                               nullptr,
                               &empty,
                               nullptr) == ERROR_SUCCESS;
}

struct locked_folder
{
  explicit locked_folder(fs::path path)
    : folder(std::move(path))
    , locked(set_empty_dacl(folder, PROTECTED_DACL_SECURITY_INFORMATION))
  {
  }

  locked_folder(const locked_folder&) = delete;

  ~locked_folder()
  {
    set_empty_dacl(folder, UNPROTECTED_DACL_SECURITY_INFORMATION);
  }

  fs::path folder;
  bool locked;
};

static void
write_file(const fs::path& file, const char* text)
{
  fs::create_directories(file.parent_path());
  std::ofstream(file) << text;
}

static fs::path
scan_folder(const char* name)
{
  const fs::path folder = exe_dir() / "scan" / name;
  fs::remove_all(folder);

  return folder;
}

TEST_CASE("scan skips unreadable folders and keeps scanning")
{
  const fs::path locked =
    exe_dir() / "scan" / "unreadable" / "a_broken" / "locked";
  set_empty_dacl(locked, UNPROTECTED_DACL_SECURITY_INFORMATION);

  const fs::path root = scan_folder("unreadable");
  write_file(locked / "hidden.txt", "hidden");
  write_file(root / "a_broken" / "ok.txt", "a");
  write_file(root / "b_after" / "after.txt", "b");

  const locked_folder lock(locked);
  REQUIRE(lock.locked);

  const scanned_mods mods = scan_mods(root);

  CHECK(mods.files.count(L"locked\\hidden.txt") == 0);
  CHECK(mods.files.count(L"ok.txt") == 1);
  CHECK(mods.files.count(L"after.txt") == 1);
}

TEST_CASE("the first mod in case-insensitive alphabetical order wins")
{
  const fs::path root = scan_folder("conflict");
  write_file(root / "B_upper" / "shared.txt", "b");
  write_file(root / "a_lower" / "shared.txt", "a");

  const scanned_mods mods = scan_mods(root);

  REQUIRE(mods.files.count(L"shared.txt") == 1);
  CHECK(mods.files.at(L"shared.txt").mod == L"a_lower");
  REQUIRE(mods.roots.size() == 2);
  CHECK(mods.roots[0].filename() == "a_lower");
  CHECK(mods.roots[1].filename() == "B_upper");
}

TEST_CASE("the mod with the higher priority wins, equal ones go by name")
{
  const fs::path root = scan_folder("priority");
  write_file(root / "a_low" / "shared.txt", "a");
  write_file(root / "b_default" / "shared.txt", "b");
  write_file(root / "c_high" / "shared.txt", "c");

  mod_config config;
  config[L"a_low"].priority = 10;
  config[L"c_high"].priority = 90;

  const scanned_mods mods = scan_mods(root, config);

  CHECK(mods.files.at(L"shared.txt").mod == L"c_high");
  REQUIRE(mods.roots.size() == 3);
  CHECK(mods.roots[0].filename() == "c_high");
  CHECK(mods.roots[1].filename() == "b_default");
  CHECK(mods.roots[2].filename() == "a_low");
}

TEST_CASE("disabled mods give no files and no plugins")
{
  const fs::path root = scan_folder("disabled");
  write_file(root / "a_off" / "shared.txt", "a");
  write_file(root / "a_off" / "plugin.asi", "");
  write_file(root / "b_on" / "shared.txt", "b");

  mod_config config;
  config[L"a_off"].enable = false;

  const scanned_mods mods = scan_mods(root, config);

  CHECK(mods.files.at(L"shared.txt").mod == L"b_on");
  CHECK(mods.plugins.empty());
  REQUIRE(mods.roots.size() == 1);
  CHECK(mods.roots[0].filename() == "b_on");
}

TEST_CASE("mod files are matched case-insensitively and keep their name")
{
  const fs::path root = scan_folder("names");
  write_file(root / "a_mod" / "Sub" / "File.TXT", "");

  const scanned_mods mods = scan_mods(root);

  REQUIRE(mods.files.count(L"sub\\file.txt") == 1);
  CHECK(mods.files.at(L"sub\\file.txt").name == L"Sub\\File.TXT");
  CHECK(mods.files.at(L"sub\\file.txt").path ==
        (root / "a_mod" / "Sub" / "File.TXT").native());
  CHECK_FALSE(mods.files.at(L"sub\\file.txt").folder);
}

TEST_CASE("mod folders are kept as folders")
{
  const fs::path root = scan_folder("folders");
  write_file(root / "a_mod" / "Sub" / "Deep" / "file.txt", "");

  const scanned_mods mods = scan_mods(root);

  REQUIRE(mods.files.count(L"sub") == 1);
  REQUIRE(mods.files.count(L"sub\\deep") == 1);
  CHECK(mods.files.at(L"sub").folder);
  CHECK(mods.files.at(L"sub\\deep").name == L"Sub\\Deep");
}

TEST_CASE("plugins are collected in mod order and are not files")
{
  const fs::path root = scan_folder("plugins");
  write_file(root / "B_mod" / "sub" / "y.ASI", "");
  write_file(root / "a_mod" / "x.asi", "");

  const scanned_mods mods = scan_mods(root);

  CHECK(mods.files.count(L"x.asi") == 0);
  CHECK(mods.files.count(L"sub\\y.asi") == 0);
  REQUIRE(mods.plugins.size() == 2);
  CHECK(mods.plugins[0].filename() == "x.asi");
  CHECK(mods.plugins[1].filename() == "y.ASI");
}

TEST_CASE("folders named like archives hold entries, not files")
{
  const fs::path root = scan_folder("archives");
  write_file(root / "a_mod" / "Stream" / "World.img" / "Bully.NFT", "");
  write_file(root / "a_mod" / "Stream" / "World.img" / "sub" / "deep.nft", "");

  const scanned_mods mods = scan_mods(root);

  CHECK(mods.files.count(L"stream") == 1);
  CHECK(mods.files.count(L"stream\\world.img") == 0);
  CHECK(mods.files.count(L"stream\\world.img\\bully.nft") == 0);
  REQUIRE(mods.archives.count(L"stream\\world.img") == 1);

  const file_overrides& entries = mods.archives.at(L"stream\\world.img");
  REQUIRE(entries.size() == 1);
  CHECK(entries.at(L"bully.nft").name == L"Stream\\World.img\\Bully.NFT");
}

TEST_CASE("scan of a missing mods folder finds nothing")
{
  const scanned_mods mods = scan_mods(exe_dir() / "scan" / "missing");

  CHECK(mods.files.empty());
  CHECK(mods.roots.empty());
  CHECK(mods.plugins.empty());
}
