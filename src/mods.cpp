#include "mods.h"
#include "log.h"

#include <Windows.h>
#include <algorithm>

namespace fs = std::filesystem;

std::wstring
lower(std::wstring s)
{
  CharLowerBuffW(s.data(), DWORD(s.size()));
  return s;
}

static std::vector<fs::path>
list_mods(const fs::path& root)
{
  std::vector<fs::path> mods;
  std::error_code error;

  for (fs::directory_iterator it(root, error), end; !error && it != end;
       it.increment(error)) {
    std::error_code ignored;

    if (it->is_directory(ignored)) {
      mods.push_back(it->path());
    }
  }

  if (error) {
    log_line(L"Error: {}: {}", root.native(), widen(error.message()));
  }

  std::ranges::sort(mods, {}, [](const fs::path& mod) {
    return lower(mod.filename().native());
  });

  return mods;
}

static bool
add_file(file_overrides& files, std::wstring key, mod_file file)
{
  auto [existing, added] = files.try_emplace(std::move(key), file);

  if (!added) {
    log_line(L"Conflict: {} from {} ignored, using {}",
             file.name,
             file.mod,
             existing->second.mod);
  }

  return added;
}

static size_t
scan_archive(const fs::path& archive, const fs::path& mod, scanned_mods& mods)
{
  size_t count = 0;
  std::error_code error;
  const std::wstring mod_name = mod.filename().native();
  const std::wstring key = lower(archive.lexically_relative(mod).native());

  for (fs::directory_iterator it(archive, error), end; !error && it != end;
       it.increment(error)) {
    std::error_code ignored;
    const fs::path& file = it->path();

    if (!it->is_regular_file(ignored)) {
      continue;
    }

    if (add_file(mods.archives[key],
                 lower(file.filename().native()),
                 mod_file{ file.native(),
                           mod_name,
                           file.lexically_relative(mod).native() })) {
      ++count;
    }
  }

  if (error) {
    log_line(L"Error: {}: {}", mod_name, widen(error.message()));
  }

  return count;
}

static void
scan_mod(const fs::path& mod, scanned_mods& mods)
{
  size_t count = 0;
  const size_t plugins_before = mods.plugins.size();
  std::error_code error;
  const std::wstring mod_name = mod.filename().native();
  const auto options = fs::directory_options::skip_permission_denied;

  for (fs::recursive_directory_iterator it(mod, options, error), end;
       !error && it != end;
       it.increment(error)) {
    std::error_code ignored;
    const fs::path& file = it->path();
    const std::wstring name = file.lexically_relative(mod).native();
    const std::wstring extension = lower(file.extension().native());

    if (it->is_directory(ignored) && extension == L".img") {
      it.disable_recursion_pending();
      count += scan_archive(file, mod, mods);
      continue;
    }

    if (it->is_directory(ignored)) {
      mods.files.try_emplace(lower(name),
                             mod_file{ file.native(), mod_name, name, true });
      continue;
    }

    if (!it->is_regular_file(ignored)) {
      continue;
    }

    if (extension == L".asi") {
      mods.plugins.push_back(file);
      continue;
    }

    if (add_file(
          mods.files, lower(name), mod_file{ file.native(), mod_name, name })) {
      ++count;
    }
  }

  if (error) {
    log_line(L"Error: {}: {}", mod_name, widen(error.message()));
  }

  log_line(L"Mod: {} - {} file(s), {} plugin(s)",
           mod_name,
           count,
           mods.plugins.size() - plugins_before);
}

scanned_mods
scan_mods(const fs::path& root)
{
  scanned_mods mods{ .roots = list_mods(root) };

  for (const fs::path& mod : mods.roots) {
    scan_mod(mod, mods);
  }

  return mods;
}
