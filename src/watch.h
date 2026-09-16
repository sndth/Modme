#pragma once

#include <filesystem>
#include <functional>

void
watch_mods(const std::filesystem::path& mods,
           const std::filesystem::path& config,
           std::function<void()> reload);
