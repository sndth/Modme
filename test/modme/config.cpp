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

TEST_CASE("config reads enable and priority per modification")
{
  const mod_config config =
    read_config(config_file("full.yaml",
                            "hot_reload: true\n"
                            "modifications:\n"
                            "  TextureOverhaulEntries:\n"
                            "    enable: false\n"
                            "    priority: 70\n"
                            "  Zażółć mod:\n"
                            "    priority: -5\n"));

  REQUIRE(config.size() == 2);
  CHECK_FALSE(config.at(L"textureoverhaulentries").enable);
  CHECK(config.at(L"textureoverhaulentries").priority == 70);
  CHECK(config.at(L"zażółć mod").enable);
  CHECK(config.at(L"zażółć mod").priority == -5);
}

TEST_CASE("config keeps defaults for wrong values")
{
  const mod_config config = read_config(config_file("wrong.yaml",
                                                    "modifications:\n"
                                                    "  a_mod:\n"
                                                    "    enable: maybe\n"
                                                    "    priority: high\n"
                                                    "  b_mod: 12\n"));

  REQUIRE(config.size() == 2);
  CHECK(config.at(L"a_mod").enable);
  CHECK(config.at(L"a_mod").priority == 50);
  CHECK(config.at(L"b_mod").priority == 50);
}

TEST_CASE("missing or broken config means defaults")
{
  CHECK(read_config(exe_dir() / "config" / "missing.yaml").empty());
  CHECK(read_config(config_file("broken.yaml", "modifications: [\n")).empty());
}
