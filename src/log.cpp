#include "log.h"

#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <share.h>

static FILE* log_file;
static std::mutex log_mutex;
static std::chrono::steady_clock::time_point log_start;

void
open_log(const std::filesystem::path& path)
{
  log_start = std::chrono::steady_clock::now();
  log_file = _wfsopen(path.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
}

void
write_log(std::wstring_view line)
{
  if (!log_file) {
    return;
  }

  std::lock_guard lock(log_mutex);
  const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - log_start)
                         .count();
  const std::wstring text = std::format(
    L"[{:02}:{:02}.{:03}] {}\n", ms / 60000, ms / 1000 % 60, ms % 1000, line);

  fputws(text.c_str(), log_file);
  fflush(log_file);
}

std::wstring
widen(std::string_view text)
{
  std::wstring wide(text.size(), L'\0');

  wide.resize(MultiByteToWideChar(
    CP_ACP, 0, text.data(), int(text.size()), wide.data(), int(wide.size())));

  return wide;
}
