#pragma once

#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

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
    fs::create_directories(g.mod / "TXD");
    fs::create_directories(g.mod / "scripts");
    fs::create_directories(g.mod / "needs_dependency");
    fs::create_directories(g.mod / "modme_missing_outside");
    fs::create_directories(g.mod / g.windows.relative_path() / "System32");
    fs::create_directories(g.polish_mod / "TXD");

    std::ofstream(g.root / "TXD" / "hud.nft") << "original";
    std::ofstream(g.root / "TXD" / "other.nft") << "untouched";
    std::ofstream(g.root / "TXD" / "config.dat") << "game";
    std::ofstream(g.mod / "TXD" / "hud.nft") << "modded";
    std::ofstream(g.mod / "TXD" / "config.dat") << "mod";
    std::ofstream(g.mod / "TXD" / "new.nft") << "new";
    std::ofstream(g.mod / "broken.asi") << "not a dll";
    std::ofstream(g.mod / "modme_missing_outside" / "missing.txt") << "outside";
    std::ofstream(g.mod / g.windows.relative_path() / "System32" /
                  "kernel32.dll")
      << "mod";
    std::ofstream(g.polish_mod / "TXD" / "hud.nft") << "conflict";
    std::ofstream(g.polish_mod / "TXD" / L"gęś.nft") << "polish";

    fs::copy_file(g.root / "Modme.asi", g.update / "Modme.asi");
    fs::copy_file(g.root / "TestPlugin.asi", g.mod / "ual_first.asi");
    fs::copy_file(g.root / "TestPlugin.asi",
                  g.mod / "scripts" / "modme_first.asi");
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
