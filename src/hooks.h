// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief File redirection, hook registration and immutable mod snapshots.

#pragma once

#include "mods.h"

#include <Windows.h>
#include <memory>
#include <span>

/// @brief A Win32 export, its detour and storage for the callable original.
struct api_hook
{
  /// @brief Export name searched in KernelBase and Kernel32.
  const char* name;
  void* detour;
  /// @brief Writable function-pointer slot populated with the trampoline.
  void** original;
};

/// @brief Create disabled MinHook detours for available Win32 exports.
/// @return Number of hooks created successfully; failures are logged.
/// @pre MinHook is initialized and each entry has valid pointer slots.
/// @note This does not enable hooks. A found export still supplies a fallback
/// original-function address when its hook cannot be created.
size_t
create_api_hooks(std::span<const api_hook> hooks);

/// @brief Resolve a path to a lowercased key inside the configured game root.
/// @return A game-relative path, or an empty string for paths outside the root
/// or paths that cannot be resolved.
/// @pre install_file_hooks() has configured the game root.
std::wstring
game_file(const wchar_t* path);

/// @brief Open an original file for synchronous reading, bypassing redirection.
/// @return A caller-owned handle, or INVALID_HANDLE_VALUE with a Win32 error.
/// @note Sharing allows other readers, writers and deletion.
/// @pre install_file_hooks() has resolved the original CreateFileW function.
HANDLE
open_for_reading(const wchar_t* path);

/// @brief Open a game-relative file, applying the current winning override.
/// @param file Lowercased relative path under the configured game root.
/// @return A caller-owned synchronous read handle, or INVALID_HANDLE_VALUE.
/// @pre install_file_hooks() has initialized redirection.
/// @sa open_for_reading()
HANDLE
open_game_file(const std::wstring& file);

/// @brief Retain the currently published immutable scan result.
/// @note Keep this shared pointer alive while using references or paths
/// borrowed from its maps. A reload can replace the global snapshot
/// concurrently.
/// @pre set_mods() has published the initial state.
std::shared_ptr<const scanned_mods>
current_mods();

/// @brief Atomically publish a scan result without invalidating old readers.
/// @note This changes future file resolution; it does not unload plugins or
/// rebuild layouts of archives that have already been opened.
void
set_mods(scanned_mods mods);

/// @brief Configure the game root, publish mods and install file/archive hooks.
/// @param root Absolute directory containing the game executable.
/// @param mods Initial scan result to publish before enabling hooks.
/// @note Called once by InitializeASI(). Failures are logged. Hooks and their
/// state live until process exit, without support for DLL unloading.
void
install_file_hooks(const std::filesystem::path& root, scanned_mods mods);
