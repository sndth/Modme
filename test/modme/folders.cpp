#include "game.h"

#include <algorithm>
#include <doctest.h>
#include <string>
#include <vector>

static std::vector<std::string>
list_a(const char* pattern)
{
  std::vector<std::string> names;
  WIN32_FIND_DATAA data{};
  HANDLE handle = FindFirstFileA(pattern, &data);

  if (handle == INVALID_HANDLE_VALUE) {
    return names;
  }

  do {
    names.push_back(data.cFileName);
  } while (FindNextFileA(handle, &data));

  CHECK(GetLastError() == ERROR_NO_MORE_FILES);
  FindClose(handle);

  return names;
}

static std::vector<std::wstring>
list_w(const wchar_t* pattern)
{
  std::vector<std::wstring> names;
  WIN32_FIND_DATAW data{};
  HANDLE handle = FindFirstFileW(pattern, &data);

  if (handle == INVALID_HANDLE_VALUE) {
    return names;
  }

  do {
    names.push_back(data.cFileName);
  } while (FindNextFileW(handle, &data));

  CHECK(GetLastError() == ERROR_NO_MORE_FILES);
  FindClose(handle);

  return names;
}

static std::vector<std::wstring>
iterate(const fs::path& folder)
{
  std::vector<std::wstring> names;

  for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
    names.push_back(entry.path().filename().native());
  }

  return names;
}

TEST_CASE("folders that exist only in mods are visible")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  const DWORD attributes = GetFileAttributesW(L"_loader\\scripts\\Menu");

  REQUIRE(attributes != INVALID_FILE_ATTRIBUTES);
  CHECK((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
  CHECK(fs::is_directory("packages"));

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(log.find("Replaced _loader\\scripts\\Menu from") == std::string::npos);
}

TEST_CASE("folder listings merge the game and all mods")
{
  REQUIRE(game().modme != nullptr);

  CHECK(iterate("_loader/scripts") ==
        std::vector<std::wstring>{ L"game.lua", L"Menu", L"Other" });
  CHECK(list_a("_loader\\scripts\\Menu\\*.lua") ==
        std::vector<std::string>{ "Main.lua" });
  CHECK(list_w(L"TXD\\*.nft") ==
        std::vector<std::wstring>{
          L"gęś.nft", L"hud.nft", L"new.nft", L"other.nft" });
  CHECK(read_a("_loader/scripts/Menu/Main.lua") == "lua");
}

TEST_CASE("merged listings show mod files and hide mod plugins")
{
  REQUIRE(game().modme != nullptr);

  WIN32_FIND_DATAW found{};
  HANDLE handle = FindFirstFileW(L"TXD\\h*.nft", &found);
  REQUIRE(handle != INVALID_HANDLE_VALUE);
  FindClose(handle);

  CHECK(found.nFileSizeLow == std::string("modded").size());
  CHECK(list_w(L"scripts\\*.asi").empty());
}

TEST_CASE("the game folder listing shows folders from mods")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  const std::vector<std::wstring> names = iterate(g.root);

  CHECK(std::ranges::find(names, L"packages") != names.end());
  CHECK(std::ranges::find(names, L"Modme.asi") != names.end());
}

TEST_CASE("DLLs from mods load by relative path")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  HMODULE module = LoadLibraryW(L"packages\\package.dll");
  REQUIRE(module != nullptr);

  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(module, path, MAX_PATH);

  CHECK(lstrcmpiW(path, (g.mod / "packages" / "package.dll").c_str()) == 0);
  CHECK(LoadLibraryA("packages/package.dll") == module);

  const std::string log = read_text(g.update / "Modme.log");
  CHECK(log.find("Replaced packages\\package.dll from TestMod") !=
        std::string::npos);
}
