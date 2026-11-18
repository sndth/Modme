// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Tracking and hook registration for virtual archive I/O.

#pragma once

#include "hooks.h"

/// @brief Log entry-map changes that cannot affect already-built archive
/// layouts.
/// @note Cached layouts remain in use until the process restarts.
void
log_archive_changes(const scanned_mods& mods);

/// @brief Register a successfully opened game IMG/DIR with entry overrides.
/// @param handle Native handle returned by the original CreateFile function.
/// @param name Requested path before ordinary-file redirection.
/// @param flags CreateFile flags, including FILE_FLAG_OVERLAPPED when present.
/// @note Only read opens are passed here. The first matching open retains an
/// archive layout and its mod snapshot. Reused handles replace stale tracking,
/// including when the new file is not an archive.
/// @sa current_mods(), archive_hooks()
void
track_archive(HANDLE handle, const wchar_t* name, DWORD flags);

/// @brief Return the process-lifetime hook table for virtual archive I/O.
/// @note ReadFile, seeking, size queries, closing and IOCP setup are covered.
/// Memory mapping, ReadFileEx and thread pool I/O are not virtualized.
std::span<const api_hook>
archive_hooks();
