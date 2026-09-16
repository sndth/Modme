#include "game.h"

#include <chrono>
#include <doctest.h>
#include <functional>
#include <thread>

static bool
eventually(const std::function<bool()>& check)
{
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (std::chrono::steady_clock::now() < end) {
    if (check()) {
      return true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return false;
}

static void
write_config(const fake_game& g, const char* text)
{
  std::ofstream(g.update / "Modme.yaml") << text;
}

static size_t
log_count(const fake_game& g, const char* line)
{
  return occurrences(read_text(g.update / "Modme.log"), line);
}

TEST_CASE("hot reload picks up new files and config changes")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  std::ofstream(g.mod / "TXD" / "hot.nft") << "hot";
  CHECK(eventually([] { return read_a("TXD\\hot.nft") == "hot"; }));

  write_config(g, "modifications:\n  off mod:\n    enable: true\n");
  CHECK(eventually([] { return read_a("TXD\\off.nft") == "off"; }));

  write_config(g,
               "modifications:\n"
               "  off mod:\n"
               "    enable: true\n"
               "  zażółć mod:\n"
               "    priority: 60\n");
  CHECK(eventually([] { return read_a("TXD\\hud.nft") == "conflict"; }));

  const size_t off = log_count(g, "] Hot reload: off\n");
  write_config(g,
               "hot_reload: false\n"
               "modifications:\n"
               "  off mod:\n"
               "    enable: false\n");
  REQUIRE(
    eventually([&] { return log_count(g, "] Hot reload: off\n") > off; }));
  CHECK(read_a("TXD\\off.nft") == "off");

  write_config(g,
               "modifications:\n"
               "  off mod:\n"
               "    enable: false\n");
  CHECK(eventually([] { return read_a("TXD\\off.nft").empty(); }));
  CHECK(read_a("TXD\\hud.nft") == "modded");
}

TEST_CASE("hot reload keeps opened archives and plugins until a restart")
{
  const fake_game& g = game();
  REQUIRE(g.modme != nullptr);

  CloseHandle(CreateFileW(L"Stream\\World.img",
                          GENERIC_READ,
                          FILE_SHARE_READ,
                          nullptr,
                          OPEN_EXISTING,
                          0,
                          nullptr));

  std::ofstream(g.mod / "Stream" / "World.img" / "later.txt") << "later";
  fs::copy_file(g.root / "TestPlugin.asi", g.mod / "later.asi");

  CHECK(eventually([&] {
    return log_count(g,
                     "] Hot reload: stream\\world.img entries change after "
                     "a restart\n") > 0 &&
           log_count(g, "] Hot reload: plugin changes need a restart\n") > 0;
  }));
  CHECK(GetModuleHandleW((g.mod / "later.asi").c_str()) == nullptr);
}
