#pragma once

#include <filesystem>
#include <vector>

void
load_plugins(const std::filesystem::path& root,
             const std::vector<std::filesystem::path>& plugins);
