#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

void
open_log(const std::filesystem::path& path);

void
write_log(std::wstring_view line);

std::wstring
widen(std::string_view text);

template<class... args_t>
void
log_line(std::wformat_string<args_t...> format, args_t&&... args)
{
  write_log(std::format(format, std::forward<args_t>(args)...));
}
