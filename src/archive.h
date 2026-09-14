#pragma once

#include "mods.h"

#include <cstdint>
#include <string>
#include <vector>

struct archive_part
{
  uint64_t offset = 0;
  uint64_t size = 0;
  const mod_file* file = nullptr;
};

struct virtual_archive
{
  std::string dir;
  std::vector<archive_part> parts;
  uint64_t size = 0;
};

virtual_archive
build_archive(std::string dir, const file_overrides& entries);
