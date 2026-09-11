#include "plugins.h"
#include "log.h"

#include <Windows.h>

void
load_plugins(const std::filesystem::path& root,
             const std::vector<std::filesystem::path>& plugins)
{
  for (auto& plugin : plugins) {
    const std::wstring name = plugin.lexically_relative(root).native();

    if (GetModuleHandleW(plugin.c_str())) {
      log_line(L"Plugin {} already loaded", name);
      continue;
    }

    HMODULE module =
      LoadLibraryExW(plugin.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (!module) {
      log_line(L"Plugin {} failed to load (error {})", name, GetLastError());
      continue;
    }

    log_line(L"Plugin {} loaded", name);

    if (auto initialize = reinterpret_cast<void (*)()>(
          GetProcAddress(module, "InitializeASI"))) {
      initialize();
    }
  }
}
