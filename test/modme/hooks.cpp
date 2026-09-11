#include "hooks.h"

#include <MinHook.h>
#include <doctest.h>

static DWORD WINAPI
fake_tick_count()
{
  return 0;
}

TEST_CASE("hooks for missing API exports are skipped, the rest are created")
{
  const MH_STATUS status = MH_Initialize();
  REQUIRE((status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED));

  void* missing = nullptr;
  void* tick_count = nullptr;
  const api_hook hooks[] = {
    { "ModmeNoSuchExport", &fake_tick_count, &missing },
    { "GetTickCount", &fake_tick_count, &tick_count },
  };

  CHECK(create_api_hooks(hooks) == 1);
  CHECK(missing == nullptr);
  CHECK(tick_count != nullptr);

  MH_Uninitialize();
}
