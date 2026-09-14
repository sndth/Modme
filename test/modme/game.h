#pragma once

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct archive_entry
{
  const char* name;
  uint32_t sectors;
  std::string text;
};

inline void
write_archive(const fs::path& path, const std::vector<archive_entry>& entries)
{
  std::ofstream dir(fs::path(path).replace_extension(".dir"), std::ios::binary);
  std::ofstream img(fs::path(path).replace_extension(".img"), std::ios::binary);
  uint32_t offset = 0;

  for (const archive_entry& entry : entries) {
    char name[24]{};
    std::string data = entry.text;

    std::copy_n(entry.name, strlen(entry.name), name);
    data.resize(entry.sectors * 2048);
    dir.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
    dir.write(reinterpret_cast<const char*>(&entry.sectors),
              sizeof(entry.sectors));
    dir.write(name, sizeof(name));
    img.write(data.data(), data.size());
    offset += entry.sectors;
  }
}

inline fs::path
exe_dir()
{
  wchar_t exe[MAX_PATH]{};

  return fs::path(exe, exe + GetModuleFileNameW(nullptr, exe, MAX_PATH))
    .parent_path();
}

inline std::string
read(HANDLE file)
{
  char buf[16]{};
  DWORD n = 0;
  const bool ok = ReadFile(file, buf, sizeof(buf) - 1, &n, nullptr);
  CloseHandle(file);

  return ok ? std::string(buf, n) : std::string();
}

inline std::string
read_a(const char* path)
{
  return read(CreateFileA(
    path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
}

inline std::string
read_w(const wchar_t* path)
{
  return read(CreateFileW(
    path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
}

inline std::string
read_text(const fs::path& file)
{
  std::ifstream in(file);

  return { std::istreambuf_iterator<char>(in), {} };
}

inline size_t
occurrences(const std::string& text, const std::string& part)
{
  size_t found = 0;

  for (size_t at = text.find(part); at != std::string::npos;
       at = text.find(part, at + part.size())) {
    ++found;
  }

  return found;
}

inline void
initialize_asi(HMODULE module)
{
  if (auto initialize =
        reinterpret_cast<void (*)()>(GetProcAddress(module, "InitializeASI"))) {
    initialize();
  }
}

inline HMODULE
load_asi(const fs::path& path)
{
  HMODULE module = LoadLibraryW(path.c_str());

  if (module) {
    initialize_asi(module);
  }

  return module;
}

struct fake_game
{
  fs::path root;
  fs::path update;
  fs::path mod;
  fs::path polish_mod;
  fs::path windows;
  std::string hud_before_init;
  bool log_before_init = false;
  HMODULE modme = nullptr;
};

inline const fake_game&
game()
{
  static const fake_game instance = [] {
    fake_game g;
    g.root = exe_dir();
    g.update = g.root / "Update";
    g.mod = g.update / "Modme" / "TestMod";
    g.polish_mod = g.update / "Modme" / L"Zażółć mod";

    wchar_t windows[MAX_PATH]{};
    g.windows =
      fs::path(windows, windows + GetWindowsDirectoryW(windows, MAX_PATH));

    fs::remove_all(g.update);
    fs::create_directories(g.root / "TXD");
    fs::create_directories(g.root / "_loader" / "scripts");
    fs::create_directories(g.mod / "TXD");
    fs::create_directories(g.mod / "scripts");
    fs::create_directories(g.mod / "packages");
    fs::create_directories(g.mod / "needs_dependency");
    fs::create_directories(g.mod / "modme_missing_outside");
    fs::create_directories(g.mod / "_loader" / "scripts" / "Menu");
    fs::create_directories(g.mod / g.windows.relative_path() / "System32");
    fs::create_directories(g.polish_mod / "TXD");
    fs::create_directories(g.polish_mod / "_loader" / "scripts" / "Other");

    std::ofstream(g.root / "TXD" / "hud.nft") << "original";
    std::ofstream(g.root / "TXD" / "other.nft") << "untouched";
    std::ofstream(g.root / "TXD" / "config.dat") << "game";
    std::ofstream(g.root / "_loader" / "scripts" / "game.lua") << "game";
    std::ofstream(g.mod / "TXD" / "hud.nft") << "modded";
    std::ofstream(g.mod / "TXD" / "config.dat") << "mod";
    std::ofstream(g.mod / "TXD" / "new.nft") << "new";
    std::ofstream(g.mod / "broken.asi") << "not a dll";
    std::ofstream(g.mod / "modme_missing_outside" / "missing.txt") << "outside";
    std::ofstream(g.mod / "_loader" / "scripts" / "Menu" / "Main.lua") << "lua";
    std::ofstream(g.mod / g.windows.relative_path() / "System32" /
                  "kernel32.dll")
      << "mod";
    std::ofstream(g.polish_mod / "TXD" / "hud.nft") << "conflict";
    std::ofstream(g.polish_mod / "TXD" / L"gęś.nft") << "polish";
    std::ofstream(g.polish_mod / "_loader" / "scripts" / "Other" / "other.lua")
      << "other";

    const fs::path mod_archive = g.mod / "Stream" / "World.img";
    const fs::path polish_archive = g.polish_mod / "Stream" / "World.img";

    fs::create_directories(g.root / "Stream");
    fs::create_directories(mod_archive);
    fs::create_directories(polish_archive);
    write_archive(g.root / "Stream" / "World",
                  { { "keep.txt", 1, "keep" },
                    { "small.txt", 2, "old small" },
                    { "grow.txt", 1, "old grow" },
                    { "twice.txt", 1, "old twice" },
                    { "twice.txt", 1, "old twice again" },
                    { "gone.txt", 0, "" } });
    std::ofstream(mod_archive / "Small.TXT") << "new small";
    std::ofstream(mod_archive / "grow.txt") << std::string(3000, 'g');
    std::ofstream(mod_archive / "twice.txt") << "new twice";
    std::ofstream(mod_archive / "added.txt") << "added";
    std::ofstream(mod_archive / "gone.txt");
    std::ofstream(mod_archive / "nothing.txt");
    std::ofstream(mod_archive / "this_name_is_longer_than_24.txt") << "long";
    std::ofstream(polish_archive / "small.txt") << "conflict";

    fs::copy_file(g.root / "Modme.asi", g.update / "Modme.asi");
    fs::copy_file(g.root / "TestPlugin.asi", g.mod / "ual_first.asi");
    fs::copy_file(g.root / "TestPlugin.asi",
                  g.mod / "scripts" / "modme_first.asi");
    fs::copy_file(g.root / "TestPlugin.asi",
                  g.mod / "packages" / "package.dll");
    fs::copy_file(g.root / "TestDependent.asi",
                  g.mod / "needs_dependency" / "dependent.asi");
    fs::copy_file(g.root / "dependency" / "TestDependency.dll",
                  g.mod / "needs_dependency" / "TestDependency.dll");

    SetCurrentDirectoryW(g.root.c_str());
    load_asi(g.mod / "ual_first.asi");
    g.modme = LoadLibraryW((g.update / "Modme.asi").c_str());
    g.hud_before_init = read_a("TXD/HUD.nft");
    g.log_before_init = fs::exists(g.update / "Modme.log");

    if (g.modme) {
      initialize_asi(g.modme);
    }

    return g;
  }();

  return instance;
}
