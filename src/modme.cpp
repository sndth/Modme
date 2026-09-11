#include "hooks.h"
#include "log.h"
#include "plugins.h"
#include "version.h"

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

static void
initialize()
{
  const fs::path asi = module_path(self_module);
  const fs::path game = module_path(nullptr).parent_path();
  const fs::path root = fs::path(asi).replace_extension();

  open_log(fs::path(asi).replace_extension(L".log"));
  log_line(L"Modme v" MODME_VERSION);
  log_line(L"Dir: {}", root.lexically_proximate(game).native());
  scanned_mods mods = scan_mods(root);
  install_file_hooks(game, std::move(mods.files), std::move(mods.roots));
  load_plugins(root, mods.plugins);
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
