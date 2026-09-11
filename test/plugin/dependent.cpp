extern "C" __declspec(dllimport) int
dependency_value();

static int init_calls;

extern "C" __declspec(dllexport) void
InitializeASI()
{
  if (dependency_value() == 7) {
    ++init_calls;
  }
}

extern "C" __declspec(dllexport) int
init_count()
{
  return init_calls;
}
