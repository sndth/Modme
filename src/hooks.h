#pragma once

#include "mods.h"

#include <Windows.h>
#include <span>

struct api_hook
{
  const char* name;
  void* detour;
  void** original;
};

size_t
create_api_hooks(std::span<const api_hook> hooks);

std::wstring
game_file(const wchar_t* path);

HANDLE
open_for_reading(const wchar_t* path);

HANDLE
open_game_file(const std::wstring& file);

void
install_file_hooks(const std::filesystem::path& root,
                   file_overrides files,
                   archive_overrides archives,
                   std::vector<std::filesystem::path> mods);
