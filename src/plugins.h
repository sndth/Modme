// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Load enabled ASIs and initialize newly loaded modules.

#pragma once

#include <filesystem>
#include <vector>

/// @brief Load enabled ASIs in order and call InitializeASI if exported.
/// @param root Mods directory, used to make plugin paths readable in the log.
/// @param plugins Paths of the enabled plugins collected by scan_mods().
/// @note Already-loaded modules are skipped. Load failures are logged without
/// stopping subsequent plugins. Dependencies can be resolved beside each ASI.
/// @note Loaded modules stay resident. Hot reload does not unload or replace
/// them.
/// @sa scan_mods()
void
load_plugins(const std::filesystem::path& root,
             const std::vector<std::filesystem::path>& plugins);
