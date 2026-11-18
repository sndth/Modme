// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Startup settings, strict reload validation and config completion.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief Per-mod settings, including defaults for omitted YAML fields.
struct mod_settings
{
  /// @brief Whether this mod supplies files, archive entries and plugins.
  bool enable = true;
  /// @brief Conflict priority; higher values are scanned first.
  int64_t priority = 50;
};

/// @brief Settings indexed by lowercased mod folder name.
using mod_config = std::unordered_map<std::wstring, mod_settings>;

/// @brief Global reload policy and per-mod settings from Modme.yaml.
struct modme_config
{
  /// @brief Whether a valid reload may publish a new mod snapshot.
  bool hot_reload = true;
  mod_config mods;
};

/// @brief Read startup settings, using defaults for missing or bad input.
/// @note Invalid individual values keep their defaults. A parsing or structural
/// exception discards the whole result. This function does not write the file.
/// @sa try_read_config()
modme_config
read_config(const std::filesystem::path& file);

/// @brief Read and validate settings before applying a hot reload.
/// @return Settings with defaults for omitted fields, or std::nullopt for an
/// unreadable, empty or invalid document. Errors are logged.
/// @note A rejected reload must leave the previously published mods active.
/// @sa read_config(), set_mods()
std::optional<modme_config>
try_read_config(const std::filesystem::path& file);

/// @brief Add missing configuration fields and discovered mods to a YAML file.
/// @param file Configuration file to create or complete.
/// @param mods Names of the mod folders on disk, including disabled mods.
/// @note Existing values and entries for removed mods are retained. Mod names
/// are matched case-insensitively. Replacement uses a temporary file; errors
/// are logged. Complete files are untouched, but a rewrite loses comments.
/// @note This fills defaults; it does not repair invalid values or broken YAML.
void
complete_config(const std::filesystem::path& file,
                const std::vector<std::wstring>& mods);
