#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct mod_file
{
  std::wstring path;
  std::wstring mod;
  std::wstring name;
};

using file_overrides = std::unordered_map<std::wstring, mod_file>;

struct scanned_mods
{
  file_overrides files;
  std::vector<std::filesystem::path> plugins;
};

std::wstring
lower(std::wstring s);

scanned_mods
scan_mods(const std::filesystem::path& root);
