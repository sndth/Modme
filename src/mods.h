// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Scan results, conflict winners and mod discovery.

#pragma once

#include "config.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief A winning file or directory and the mod that supplied it.
struct mod_file
{
  /// @brief Source path used to open the mod file on disk.
  std::wstring path;
  /// @brief Display name of the owning mod folder.
  std::wstring mod;
  /// @brief Original path relative to the mod folder, preserving case.
  std::wstring name;
  /// @brief Whether this is an ordinary directory rather than a file.
  bool folder = false;

  bool operator==(const mod_file&) const = default;
};

/// @brief Winners indexed by lowercased game-relative path or archive entry
/// name.
/// @note Keys use Windows separators. The containing map decides whether each
/// key is a complete path or just an archive entry name.
using file_overrides = std::unordered_map<std::wstring, mod_file>;
/// @brief Per-archive winners indexed by lowercased game-relative IMG path.
using archive_overrides = std::unordered_map<std::wstring, file_overrides>;

/// @brief A scan result published as an immutable snapshot to the file hooks.
/// @note This owns path metadata, not copies of file contents.
/// @sa current_mods(), set_mods()
struct scanned_mods
{
  file_overrides files;
  archive_overrides archives;
  /// @brief Enabled mod roots, ordered by priority and then folder name.
  std::vector<std::filesystem::path> roots;
  /// @brief ASIs in scan order, excluded from ordinary file overrides.
  /// @note Order within one mod follows filesystem enumeration.
  std::vector<std::filesystem::path> plugins;
};

/// @brief Apply Windows character lowercasing without normalizing path syntax.
std::wstring
lower(std::wstring s);

/// @brief List immediate mod directories in case-insensitive name order.
/// @note Configuration switches are not applied here; scan errors are logged.
std::vector<std::wstring>
mod_folders(const std::filesystem::path& root);

/// @brief Scan enabled mods and resolve file and archive-entry conflicts.
/// @param root Directory containing one subdirectory per mod.
/// @param config Overrides of the default enabled state and priority.
/// @return Winners from higher-priority mods; ties are resolved by folder name.
/// @note Directories ending in .img supply archive entries; ASIs are collected
/// for plugin loading. Scanning neither installs hooks nor loads plugins.
scanned_mods
scan_mods(const std::filesystem::path& root, const mod_config& config = {});
