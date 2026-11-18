// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

/// @file
/// @brief Timestamped logging and conversion of Windows diagnostics.

#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

/// @brief Start a timestamped UTF-8 log, truncating any existing file.
/// @note Initialize once before starting workers. If opening fails, later
/// logging calls do nothing.
void
open_log(const std::filesystem::path& path);

/// @brief Serialize, timestamp and flush one line to the log.
/// @note Calls from different threads are protected by a mutex.
void
write_log(std::wstring_view line);

/// @brief Convert Windows/CRT diagnostics using the system ANSI code page.
/// @note This is not a UTF-8 decoder; YAML text is converted separately.
std::wstring
widen(std::string_view text);

/// @brief Format a wide message and pass it to write_log().
template<class... args_t>
void
log_line(std::wformat_string<args_t...> format, args_t&&... args)
{
  write_log(std::format(format, std::forward<args_t>(args)...));
}
