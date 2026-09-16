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

TEST_CASE("a missing config is generated with every mod")
{
  const fs::path file = exe_dir() / "config" / "generated.yaml";
  fs::remove(file);

  complete_config(file, { L"B mod", L"Zażółć mod" });
  const modme_config config = read_config(file);

  CHECK(config.hot_reload);
  REQUIRE(config.mods.size() == 2);
  CHECK(config.mods.at(L"b mod").enable);
  CHECK(config.mods.at(L"zażółć mod").priority == 50);
  CHECK(read_text(file).find("priority: 50") != std::string::npos);
}

TEST_CASE("a config gets only what it misses")
{
  const fs::path file = config_file("partial.yaml",
                                    "hot_reload: false\n"
                                    "modifications:\n"
                                    "  A mod:\n"
                                    "    priority: 70\n"
                                    "  Gone mod:\n"
                                    "    enable: false\n");

  complete_config(file, { L"a MOD", L"New mod" });
  const modme_config config = read_config(file);

  CHECK_FALSE(config.hot_reload);
  REQUIRE(config.mods.size() == 3);
  CHECK(config.mods.at(L"a mod").enable);
  CHECK(config.mods.at(L"a mod").priority == 70);
  CHECK_FALSE(config.mods.at(L"gone mod").enable);
  CHECK(config.mods.at(L"new mod").priority == 50);
  CHECK(read_text(file).find("a MOD") == std::string::npos);
}

TEST_CASE("complete or broken configs are left as they are")
{
  const char* complete = "hot_reload: true\n"
                         "modifications:\n"
                         "  A: {enable: true, priority: 50}\n";
  const char* broken = "hot_reload: false\nmodifications: [\n";

  complete_config(config_file("complete.yaml", complete), { L"A" });
  complete_config(config_file("broken_kept.yaml", broken), { L"A" });

  CHECK(read_text(exe_dir() / "config" / "complete.yaml") == complete);
  CHECK(read_text(exe_dir() / "config" / "broken_kept.yaml") == broken);
}

TEST_CASE("the game config gets the mods found at startup")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  const modme_config config = read_config(g.update / "Modme.yaml");

  CHECK(config.mods.at(L"testmod").priority == 50);
  CHECK(config.mods.at(L"zażółć mod").enable);
  CHECK_FALSE(config.mods.at(L"off mod").enable);
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
