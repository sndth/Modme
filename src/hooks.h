#pragma once

#include "mods.h"

#include <span>

struct api_hook
{
  const char* name;
  void* detour;
  void** original;
};

size_t
create_api_hooks(std::span<const api_hook> hooks);

void
install_file_hooks(const std::filesystem::path& root,
                   file_overrides files,
                   std::vector<std::filesystem::path> mods);
