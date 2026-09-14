#pragma once

#include "hooks.h"

void
set_archives(archive_overrides archives);

void
track_archive(HANDLE handle, const wchar_t* name, DWORD flags);

std::span<const api_hook>
archive_hooks();
