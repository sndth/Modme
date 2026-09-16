#include "config.h"
#include "log.h"
#include "mods.h"

#include <Windows.h>
#include <fkYAML/node.hpp>
#include <fstream>

static std::wstring
from_utf8(const std::string& text)
{
  std::wstring wide(text.size(), L'\0');

  wide.resize(MultiByteToWideChar(
    CP_UTF8, 0, text.data(), int(text.size()), wide.data(), int(wide.size())));

  return wide;
}

static std::string
to_utf8(const std::wstring& text)
{
  std::string narrow(text.size() * 3, '\0');

  narrow.resize(WideCharToMultiByte(CP_UTF8,
                                    0,
                                    text.data(),
                                    int(text.size()),
                                    narrow.data(),
                                    int(narrow.size()),
                                    nullptr,
                                    nullptr));

  return narrow;
}

static bool
add_missing(fkyaml::node& node, const char* key, fkyaml::node value)
{
  if (node.contains(key)) {
    return false;
  }

  node[key] = std::move(value);

  return true;
}

static bool
complete_mods(fkyaml::node& modifications,
              const std::vector<std::wstring>& mods)
{
  bool changed = false;

  for (const std::wstring& mod : mods) {
    fkyaml::node* entry = nullptr;

    for (auto& [key, value] : modifications.as_map()) {
      if (key.is_string() && lower(from_utf8(key.as_str())) == lower(mod)) {
        entry = &value;
      }
    }

    if (!entry) {
      entry = &modifications[to_utf8(mod)];
      *entry = fkyaml::node::mapping();
      changed = true;
    }

    if (entry->is_mapping()) {
      changed |= add_missing(*entry, "enable", true);
      changed |= add_missing(*entry, "priority", 50);
    }
  }

  return changed;
}

static mod_settings
read_settings(const std::wstring& name, const fkyaml::node& node)
{
  mod_settings settings;

  if (!node.is_mapping()) {
    log_line(L"Config: {} must be a mapping", name);
    return settings;
  }

  if (node.contains("enable") && node["enable"].is_boolean()) {
    settings.enable = node["enable"].as_bool();
  } else if (node.contains("enable")) {
    log_line(L"Config: {} enable must be true or false", name);
  }

  if (node.contains("priority") && node["priority"].is_integer()) {
    settings.priority = node["priority"].as_int();
  } else if (node.contains("priority")) {
    log_line(L"Config: {} priority must be a whole number", name);
  }

  return settings;
}

static void
read_root(const fkyaml::node& root, modme_config& config)
{
  if (!root.is_mapping()) {
    return;
  }

  if (root.contains("hot_reload") && root["hot_reload"].is_boolean()) {
    config.hot_reload = root["hot_reload"].as_bool();
  } else if (root.contains("hot_reload")) {
    log_line(L"Config: hot_reload must be true or false");
  }

  if (!root.contains("modifications")) {
    return;
  }

  for (const auto& [key, value] : root["modifications"].as_map()) {
    const std::wstring name = from_utf8(key.as_str());

    config.mods.insert_or_assign(lower(name), read_settings(name, value));
  }
}

modme_config
read_config(const std::filesystem::path& file)
{
  modme_config config;
  std::ifstream in(file, std::ios::binary);

  if (!in) {
    return config;
  }

  try {
    read_root(fkyaml::node::deserialize(in), config);
  } catch (const std::exception& error) {
    log_line(L"Config: {}", widen(error.what()));
    config = {};
  }

  return config;
}

void
complete_config(const std::filesystem::path& file,
                const std::vector<std::wstring>& mods)
{
  fkyaml::node root = fkyaml::node::mapping();
  std::ifstream in(file, std::ios::binary);
  const bool found = bool(in);

  try {
    if (found) {
      root = fkyaml::node::deserialize(in);
      in.close();
    }

    if (root.is_null()) {
      root = fkyaml::node::mapping();
    }

    if (!root.is_mapping()) {
      return;
    }

    bool changed = !found;

    changed |= add_missing(root, "hot_reload", true);

    if (!root.contains("modifications") || root["modifications"].is_null()) {
      root["modifications"] = fkyaml::node::mapping();
      changed = true;
    }

    if (root["modifications"].is_mapping()) {
      changed |= complete_mods(root["modifications"], mods);
    }

    if (changed) {
      std::ofstream(file, std::ios::binary) << fkyaml::node::serialize(root);
      log_line(L"Config: updated {}", file.filename().native());
    }
  } catch (const std::exception&) {
  }
}
