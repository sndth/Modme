#include "config.h"
#include "game.h"

#include <doctest.h>

static fs::path
config_file(const char* name, const char* text)
{
  const fs::path file = exe_dir() / "config" / name;

  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;

  return file;
}

TEST_CASE("config reads hot reload, enable and priority per modification")
{
  const modme_config config =
    read_config(config_file("full.yaml",
                            "hot_reload: false\n"
                            "modifications:\n"
                            "  TextureOverhaulEntries:\n"
                            "    enable: false\n"
                            "    priority: 70\n"
                            "  Zażółć mod:\n"
                            "    priority: -5\n"));

  CHECK_FALSE(config.hot_reload);
  REQUIRE(config.mods.size() == 2);
  CHECK_FALSE(config.mods.at(L"textureoverhaulentries").enable);
  CHECK(config.mods.at(L"textureoverhaulentries").priority == 70);
  CHECK(config.mods.at(L"zażółć mod").enable);
  CHECK(config.mods.at(L"zażółć mod").priority == -5);
}

TEST_CASE("config keeps defaults for wrong values")
{
  const modme_config config = read_config(config_file("wrong.yaml",
                                                      "hot_reload: sometimes\n"
                                                      "modifications:\n"
                                                      "  a_mod:\n"
                                                      "    enable: maybe\n"
                                                      "    priority: high\n"
                                                      "  b_mod: 12\n"));

  CHECK(config.hot_reload);
  REQUIRE(config.mods.size() == 2);
  CHECK(config.mods.at(L"a_mod").enable);
  CHECK(config.mods.at(L"a_mod").priority == 50);
  CHECK(config.mods.at(L"b_mod").priority == 50);
}

TEST_CASE("missing or broken config means defaults")
{
  const modme_config missing =
    read_config(exe_dir() / "config" / "missing.yaml");
  const modme_config broken = read_config(
    config_file("broken.yaml", "hot_reload: false\nmodifications: [\n"));

  CHECK(missing.hot_reload);
  CHECK(missing.mods.empty());
  CHECK(broken.hot_reload);
  CHECK(broken.mods.empty());
}
