#pragma once

#include "mods.h"

#include <Windows.h>
#include <memory>
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

std::shared_ptr<const scanned_mods>
current_mods();

void
set_mods(scanned_mods mods);

void
install_file_hooks(const std::filesystem::path& root, scanned_mods mods);
