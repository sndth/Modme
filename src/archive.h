// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Virtual IMG layouts and borrowed replacement ranges.

#pragma once

#include "mods.h"

#include <cstdint>
#include <string>
#include <vector>

/// @brief A byte range backed by the original IMG or a replacement file.
/// @note Offsets and lengths are in bytes, including sector padding.
struct archive_part
{
  uint64_t offset = 0;
  uint64_t size = 0;
  /// @brief Replacement source, or nullptr to read the original IMG.
  /// @note Borrowed from the entry map passed to build_archive().
  const mod_file* file = nullptr;
};

/// @brief An in-memory DIR table and its virtual IMG layout.
/// @note Replacement bytes are read on demand, not stored in this object.
struct virtual_archive
{
  /// @brief Serialized 32-byte DIR records with updated offsets and sizes.
  std::string dir;
  /// @brief Non-overlapping ranges ordered by byte offset.
  std::vector<archive_part> parts;
  uint64_t size = 0;
};

/// @brief Build an IMG layout without changing the archive on disk.
/// @param dir Raw DIR records: sector offset, sector count and a 24-byte name.
/// @param entries Replacements indexed by lowercased entry name.
/// @return A layout replacing fitting entries in place, appending larger or new
/// entries, and removing existing entries replaced by empty files.
/// @pre dir uses the external DIR format with 2048-byte sectors.
/// @note entries must outlive the result: archive_part::file borrows its
/// values. Unsupported names and additions exceeding 4 GB are logged and
/// skipped. Arbitrary archive formats are not validated or detected here.
/// @sa archive_part
virtual_archive
build_archive(std::string dir, const file_overrides& entries);
