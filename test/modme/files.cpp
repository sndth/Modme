#include "game.h"

#include <doctest.h>

TEST_CASE("nothing happens before InitializeASI")
{
  REQUIRE(game().modme != nullptr);

  CHECK(game().hud_before_init == "original");
  CHECK_FALSE(game().log_before_init);
}

TEST_CASE("mod files replace game files")
{
  REQUIRE(game().modme != nullptr);

  CHECK(read_a("TXD/HUDElems/../HUD.NFT") == "modded");
  CHECK(read_w(L"txd\\hud.nft") == "modded");
  CHECK(read_a("TXD\\other.nft") == "untouched");
  CHECK(read_a("TXD/new.nft") == "new");
}

TEST_CASE("the first mod in alphabetical order wins a conflict")
{
  REQUIRE(game().modme != nullptr);

  CHECK(read_a("TXD/hud.nft") == "modded");
}

TEST_CASE("absolute paths are redirected")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  CHECK(read_w((g.root / "TXD" / "hud.nft").c_str()) == "modded");
  CHECK(read_a((g.root / "txd" / "HUD.nft").string().c_str()) == "modded");
  CHECK(read_w((g.root / "TXD" / "other.nft").c_str()) == "untouched");
}

TEST_CASE("paths with Polish characters are redirected")
{
  REQUIRE(game().modme != nullptr);

  CHECK(read_w(L"TXD\\gęś.nft") == "polish");
  CHECK(read_w(L"txd/GĘŚ.NFT") == "polish");
}

TEST_CASE("attribute and find queries see mod files")
{
  REQUIRE(game().modme != nullptr);

  CHECK(GetFileAttributesA("TXD/new.nft") != INVALID_FILE_ATTRIBUTES);
  CHECK(GetFileAttributesW(L"txd\\NEW.nft") != INVALID_FILE_ATTRIBUTES);

  WIN32_FILE_ATTRIBUTE_DATA info_a{};
  REQUIRE(GetFileAttributesExA("TXD/hud.nft", GetFileExInfoStandard, &info_a));
  CHECK(info_a.nFileSizeLow == std::string("modded").size());

  WIN32_FILE_ATTRIBUTE_DATA info_w{};
  REQUIRE(
    GetFileAttributesExW(L"TXD\\hud.nft", GetFileExInfoStandard, &info_w));
  CHECK(info_w.nFileSizeLow == std::string("modded").size());

  WIN32_FIND_DATAA found_a{};
  HANDLE find_a = FindFirstFileA("TXD\\new.nft", &found_a);
  REQUIRE(find_a != INVALID_HANDLE_VALUE);
  FindClose(find_a);
  CHECK(std::string(found_a.cFileName) == "new.nft");
  CHECK(found_a.nFileSizeLow == std::string("new").size());

  WIN32_FIND_DATAW found_w{};
  HANDLE find_w = FindFirstFileW(L"TXD/hud.nft", &found_w);
  REQUIRE(find_w != INVALID_HANDLE_VALUE);
  FindClose(find_w);
  CHECK(found_w.nFileSizeLow == std::string("modded").size());
}

TEST_CASE("files opened for writing stay the game's")
{
  REQUIRE(game().modme != nullptr);

  HANDLE file = CreateFileA("TXD/config.dat",
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            nullptr);
  REQUIRE(file != INVALID_HANDLE_VALUE);

  CHECK(read(file) == "game");
  CHECK(read_a("TXD/config.dat") == "mod");
}

TEST_CASE("files outside the game folder come from mods only when missing")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  const fs::path missing =
    g.windows.root_path() / "modme_missing_outside" / "missing.txt";
  const fs::path existing = g.windows / "System32" / "kernel32.dll";

  CHECK(read_w(missing.c_str()) == "outside");
  CHECK(read_a(missing.string().c_str()) == "outside");
  CHECK(GetFileAttributesW(missing.c_str()) != INVALID_FILE_ATTRIBUTES);
  CHECK(read_w(existing.c_str()).starts_with("MZ"));
}

TEST_CASE("InitializeASI runs only once")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  initialize_asi(g.modme);
  CHECK(read_a("Txd/New.nft") == "new");

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(occurrences(log, "] Dir: ") == 1);
  CHECK(occurrences(log, "Replaced TXD\\new.nft from TestMod") == 1);
}
