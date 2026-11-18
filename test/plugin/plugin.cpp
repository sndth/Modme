// SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
// SPDX-License-Identifier: MIT

static int init_calls;

extern "C" __declspec(dllexport) void
InitializeASI()
{
  ++init_calls;
}

extern "C" __declspec(dllexport) int
init_count()
{
  return init_calls;
}
