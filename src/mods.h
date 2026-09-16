#pragma once

#include "config.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct mod_file
{
  std::wstring path;
  std::wstring mod;
  std::wstring name;
  bool folder = false;

  bool operator==(const mod_file&) const = default;
};

using file_overrides = std::unordered_map<std::wstring, mod_file>;
using archive_overrides = std::unordered_map<std::wstring, file_overrides>;

struct scanned_mods
{
  file_overrides files;
  archive_overrides archives;
  std::vector<std::filesystem::path> roots;
  std::vector<std::filesystem::path> plugins;
};

std::wstring
lower(std::wstring s);

scanned_mods
scan_mods(const std::filesystem::path& root, const mod_config& config = {});
