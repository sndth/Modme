#include "game.h"
#include "version.h"

#include <doctest.h>
#include <vector>

TEST_CASE("log lists mods, conflicts, replaced files and plugins")
{
  const fake_game& g = game();

  read_a("TXD/HUDElems/../HUD.NFT");
  read_a("TXD\\other.nft");
  CloseHandle(CreateFileA("TXD/config.dat",
                          GENERIC_WRITE,
                          FILE_SHARE_READ,
                          nullptr,
                          OPEN_EXISTING,
                          0,
                          nullptr));
  read_w((g.windows / "System32" / "kernel32.dll").c_str());

  const std::string log = read_text(g.update / "Modme.log");

  CHECK(log.find("[00:00.") != std::string::npos);
  CHECK(log.find("] Modme v" MODME_VERSION "\n") != std::string::npos);
  CHECK(log.find("] Dir: Update\\Modme\n") != std::string::npos);
  CHECK(log.find("Mod: TestMod - 13 file(s), 4 plugin(s)") !=
        std::string::npos);
  CHECK(log.find("Mod: Zażółć mod - 2 file(s), 0 plugin(s)") !=
        std::string::npos);
  CHECK(log.find("Conflict: TXD\\hud.nft from Zażółć mod ignored, using "
                 "TestMod") != std::string::npos);
  CHECK(log.find("] Hooks: 19/19\n") != std::string::npos);

  CHECK(log.find("Replaced TXD\\hud.nft from TestMod") != std::string::npos);
  CHECK(log.find("other.nft") == std::string::npos);
  CHECK(log.find("Skipped TXD\\config.dat from TestMod, opened for writing") !=
        std::string::npos);
  CHECK(log.find("kernel32.dll from TestMod, exists outside the game folder") !=
        std::string::npos);

  CHECK(log.find("Plugin TestMod\\ual_first.asi already loaded") !=
        std::string::npos);
  CHECK(log.find("Plugin TestMod\\scripts\\modme_first.asi loaded") !=
        std::string::npos);
  CHECK(log.find("Plugin TestMod\\needs_dependency\\dependent.asi loaded") !=
        std::string::npos);
  CHECK(log.find("Plugin TestMod\\broken.asi failed to load (error 193)") !=
        std::string::npos);
}

TEST_CASE("log mentions each replaced file once")
{
  const fake_game& g = game();

  read_a("TXD\\NEW.NFT");
  read_w(L"txd/new.nft");

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(occurrences(log, "Replaced TXD\\new.nft from TestMod") == 1);
}

TEST_CASE("log says when the mods folder is missing")
{
  const fs::path folder = exe_dir() / "no_mods";
  fs::remove_all(folder);
  fs::create_directories(folder);
  fs::copy_file(exe_dir() / "Modme.asi", folder / "Empty.asi");

  REQUIRE(load_asi(folder / "Empty.asi") != nullptr);

  const std::string log = read_text(folder / "Empty.log");
  CHECK(log.find("] Mods folder not found: no_mods\\Empty\n") !=
        std::string::npos);
  CHECK(log.find("Error:") == std::string::npos);
  CHECK(log.find("Hooks: skipped, no mod files") != std::string::npos);
}

TEST_CASE("Modme.asi carries its version")
{
  const std::wstring asi = (game().update / "Modme.asi").native();
  const DWORD size = GetFileVersionInfoSizeW(asi.c_str(), nullptr);
  REQUIRE(size > 0);

  std::vector<char> data(size);
  REQUIRE(GetFileVersionInfoW(asi.c_str(), 0, size, data.data()));

  LPVOID version = nullptr;
  UINT length = 0;
  REQUIRE(VerQueryValueW(data.data(),
                         L"\\StringFileInfo\\040904b0\\FileVersion",
                         &version,
                         &length));

  CHECK(std::wstring(static_cast<const wchar_t*>(version)) ==
        L"" MODME_VERSION);
}
