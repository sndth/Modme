#include "archive_hooks.h"
#include "hooks.h"
#include "log.h"
#include "plugins.h"
#include "version.h"
#include "watch.h"

#include <Windows.h>
#include <mutex>

namespace fs = std::filesystem;

static HMODULE self_module = nullptr;

static fs::path
module_path(HMODULE module)
{
  wchar_t buf[MAX_PATH * 2]{};
  return { buf, buf + GetModuleFileNameW(module, buf, DWORD(std::size(buf))) };
}

static modme_config
load_config(const fs::path& root, const fs::path& config_file)
{
  complete_config(config_file, mod_folders(root));

  return read_config(config_file);
}

static void
reload(const fs::path& root,
       const fs::path& config_file,
       const std::vector<fs::path>& plugins)
{
  const modme_config config = load_config(root, config_file);

  if (!config.hot_reload) {
    log_line(L"Hot reload: off");
    return;
  }

  log_line(L"Hot reload");
  scanned_mods mods = scan_mods(root, config.mods);

  if (mods.plugins != plugins) {
    log_line(L"Hot reload: plugin changes need a restart");
  }

  log_archive_changes(mods);
  set_mods(std::move(mods));
}

static void
initialize()
{
  const fs::path asi = module_path(self_module);
  const fs::path game = module_path(nullptr).parent_path();
  const fs::path root = fs::path(asi).replace_extension();
  const fs::path config_file = fs::path(asi).replace_extension(L".yaml");

  open_log(fs::path(asi).replace_extension(L".log"));
  log_line(L"Modme v" MODME_VERSION);
  const fs::path dir = root.lexically_proximate(game);
  std::error_code error;

  log_line(L"Dir: {}", dir.native());

  if (!fs::is_directory(root, error)) {
    log_line(L"Mods folder not found: {}", dir.native());
    return;
  }

  scanned_mods mods = scan_mods(root, load_config(root, config_file).mods);
  const std::vector<fs::path> plugins = mods.plugins;

  install_file_hooks(game, std::move(mods));
  load_plugins(root, plugins);
  watch_mods(root, config_file, [=] { reload(root, config_file, plugins); });
}

extern "C" __declspec(dllexport) void
InitializeASI()
{
  static std::once_flag once;
  std::call_once(once, initialize);
}

BOOL WINAPI
DllMain(HINSTANCE self, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH) {
    self_module = self;
  }

  return TRUE;
}
