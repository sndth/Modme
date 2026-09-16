#pragma once

#include "hooks.h"

void
log_archive_changes(const scanned_mods& mods);

void
track_archive(HANDLE handle, const wchar_t* name, DWORD flags);

std::span<const api_hook>
archive_hooks();
