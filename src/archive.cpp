#include "archive.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <ranges>
#include <unordered_map>

namespace fs = std::filesystem;

struct dir_entry
{
  uint32_t offset;
  uint32_t size;
  char name[24];
};

struct entry_file
{
  const mod_file* file;
  std::string name;
  uint64_t sectors;
  bool found = false;
};

static const uint64_t sector = 2048;
static const uint64_t max_size = 0xFFFFFFFF;

static std::string
ascii_name(const std::wstring& path)
{
  const std::wstring name = path.substr(path.rfind(L'\\') + 1);
  std::string ascii;

  if (name.size() > sizeof(dir_entry::name)) {
    return {};
  }

  for (wchar_t c : name) {
    if (c == 0 || c >= 128) {
      return {};
    }

    ascii.push_back(char(c));
  }

  return ascii;
}

static std::wstring
entry_key(const dir_entry& entry)
{
  const size_t length = strnlen(entry.name, sizeof(entry.name));
  std::wstring key;

  for (size_t i = 0; i < length; ++i) {
    key.push_back(wchar_t(static_cast<unsigned char>(entry.name[i])));
  }

  return lower(std::move(key));
}

static std::map<std::wstring, entry_file>
entry_files(const file_overrides& entries)
{
  std::map<std::wstring, entry_file> files;

  for (const auto& [key, file] : entries) {
    std::error_code error;
    const uintmax_t size = fs::file_size(file.path, error);
    std::string name = ascii_name(file.name);

    if (error) {
      log_line(
        L"Skipped {} from {}, {}", file.name, file.mod, widen(error.message()));
    } else if (name.empty()) {
      log_line(L"Skipped {} from {}, entry names are ASCII and up to 24 "
               L"characters",
               file.name,
               file.mod);
    } else {
      files.emplace(
        key,
        entry_file{ &file, std::move(name), (size + sector - 1) / sector });
    }
  }

  return files;
}

virtual_archive
build_archive(std::string dir, const file_overrides& entries)
{
  virtual_archive archive;
  std::map<std::wstring, entry_file> files = entry_files(entries);
  std::unordered_map<const mod_file*, uint32_t> moved;
  std::vector<archive_part> replaced;
  std::vector<dir_entry> table(dir.size() / sizeof(dir_entry));

  std::memcpy(table.data(), dir.data(), table.size() * sizeof(dir_entry));

  for (const dir_entry& entry : table) {
    archive.size = std::max<uint64_t>(
      archive.size, (uint64_t(entry.offset) + entry.size) * sector);
  }

  const uint64_t base = archive.size;
  const auto place = [&](const entry_file& entry) -> std::optional<uint32_t> {
    if (auto it = moved.find(entry.file); it != moved.end()) {
      return it->second;
    }

    const uint64_t bytes = entry.sectors * sector;

    if (archive.size + bytes > max_size) {
      log_line(L"Skipped {} from {}, the archive would grow past 4 GB",
               entry.file->name,
               entry.file->mod);
      return std::nullopt;
    }

    const uint32_t at = uint32_t(archive.size / sector);

    replaced.push_back({ archive.size, bytes, entry.file });
    archive.size += bytes;
    moved.emplace(entry.file, at);

    return at;
  };

  for (dir_entry& entry : table) {
    auto it = files.find(entry_key(entry));

    if (it == files.end()) {
      continue;
    }

    it->second.found = true;

    if (it->second.sectors <= entry.size) {
      replaced.push_back({ uint64_t(entry.offset) * sector,
                           uint64_t(entry.size) * sector,
                           it->second.file });
      entry.size = uint32_t(it->second.sectors);
      continue;
    }

    if (auto at = place(it->second)) {
      entry.offset = *at;
      entry.size = uint32_t(it->second.sectors);
    }
  }

  for (const entry_file& entry : files | std::views::values) {
    if (entry.found) {
      continue;
    }

    if (auto at = place(entry)) {
      dir_entry added{ *at, uint32_t(entry.sectors), {} };

      std::ranges::copy(entry.name, added.name);
      table.push_back(added);
    }
  }

  archive.dir.assign(reinterpret_cast<const char*>(table.data()),
                     table.size() * sizeof(dir_entry));
  std::ranges::sort(replaced, {}, &archive_part::offset);

  uint64_t at = 0;

  for (const archive_part& part : replaced) {
    if (part.size == 0 || part.offset < at) {
      continue;
    }

    if (part.offset > at) {
      archive.parts.push_back({ at, part.offset - at });
    }

    archive.parts.push_back(part);
    at = part.offset + part.size;
  }

  if (at < base) {
    archive.parts.push_back({ at, base - at });
  }

  return archive;
}
