// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Process-lifetime watchers for mods and their YAML configuration.

#pragma once

#include <filesystem>
#include <functional>

/// @brief Watch mods and the config, invoking reload after 500 ms of quiet.
/// @param mods Directory to watch recursively for file and folder changes.
/// @param config YAML file whose parent directory is watched for name matches.
/// @param reload Callback invoked serially on a background worker.
/// @note Startup waits for each watch to be queued or report a setup error.
/// Threads are detached and live until process exit; captures used by reload
/// must stay valid for that lifetime, and the callback must not throw.
/// @sa
/// https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
void
watch_mods(const std::filesystem::path& mods,
           const std::filesystem::path& config,
           std::function<void()> reload);
