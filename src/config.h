#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

struct mod_settings
{
  bool enable = true;
  int64_t priority = 50;
};

using mod_config = std::unordered_map<std::wstring, mod_settings>;

mod_config
read_config(const std::filesystem::path& file);
