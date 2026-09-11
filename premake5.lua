workspace "Modme"
  symbols "On"
  location "build"
  cppdialect "C++23"
  architecture "x86"
  staticruntime "On"

  buildoptions {
    "/utf-8"
  }

  configurations {
    "Debug",
    "Release"
  }

  filter "configurations:Release"
    optimize "Speed"

  project "Modme"
    kind "SharedLib"
    targetextension ".asi"

    files {
      "src/**",
      "vendor/minhook/include/*.h",
      "vendor/minhook/src/*.c",
      "vendor/minhook/src/*.h",
      "vendor/minhook/src/hde/hde32.*",
      "vendor/minhook/src/hde/pstdint.h",
      "vendor/minhook/src/hde/table32.h"
    }

    vpaths {
      ["*"] = "src/**",
      ["MinHook/*"] = "vendor/minhook/**"
    }

    includedirs {
      "vendor/minhook/include"
    }
