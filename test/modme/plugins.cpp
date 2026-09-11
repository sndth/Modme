#include "game.h"

#include <doctest.h>

static int
init_count(const fs::path& plugin)
{
  HMODULE module = GetModuleHandleW(plugin.c_str());

  if (!module) {
    return 0;
  }

  auto count =
    reinterpret_cast<int (*)()>(GetProcAddress(module, "init_count"));

  return count ? count() : 0;
}

TEST_CASE("plugins find DLLs placed next to them")
{
  REQUIRE(game().modme != nullptr);

  CHECK(init_count(game().mod / "needs_dependency" / "dependent.asi") == 1);
}

TEST_CASE("mod plugins are initialized once")
{
  REQUIRE(game().modme != nullptr);

  CHECK(init_count(game().mod / "ual_first.asi") == 1);
  CHECK(init_count(game().mod / "scripts" / "modme_first.asi") == 1);
}

TEST_CASE("a plugin that fails to load is skipped")
{
  REQUIRE(game().modme != nullptr);

  CHECK(GetModuleHandleW((game().mod / "broken.asi").c_str()) == nullptr);
  CHECK(init_count(game().mod / "scripts" / "modme_first.asi") == 1);
}

TEST_CASE("a mod with only plugins loads them without hooking files")
{
  const fs::path root = exe_dir() / "only_plugins";
  fs::remove_all(root);
  fs::create_directories(root / "Mods" / "Plugins");
  fs::copy_file(exe_dir() / "Modme.asi", root / "Mods.asi");
  fs::copy_file(exe_dir() / "TestPlugin.asi",
                root / "Mods" / "Plugins" / "only.asi");

  REQUIRE(load_asi(root / "Mods.asi") != nullptr);
  CHECK(init_count(root / "Mods" / "Plugins" / "only.asi") == 1);

  const std::string log = read_text(root / "Mods.log");
  CHECK(log.find("Mod: Plugins - 0 file(s), 1 plugin(s)") != std::string::npos);
  CHECK(log.find("Hooks: skipped, no mod files") != std::string::npos);
  CHECK(log.find("Hooks: 8/8") == std::string::npos);
  CHECK(log.find("Plugin Plugins\\only.asi loaded") != std::string::npos);
}
