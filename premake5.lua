local minhook = {
  "vendor/minhook/src/*.c",
  "vendor/minhook/src/*.h",
  "vendor/minhook/include/*.h",
  "vendor/minhook/src/hde/hde32.*",
  "vendor/minhook/src/hde/pstdint.h",
  "vendor/minhook/src/hde/table32.h"
}

workspace "Modme"
  symbols "On"
  location "build"
  cppdialect "C++23"
  architecture "x86"
  startproject "TestModme"
  staticruntime "On"

  buildoptions {
    "/utf-8"
  }

  configurations {
    "Debug",
    "Release"
  }

  vpaths {
    ["*"] = {
      "src/**",
      "test/plugin/**"
    },
    ["Tests/*"] = "test/modme/**",
    ["MinHook/*"] = "vendor/minhook/**"
  }

  filter "configurations:Release"
    optimize "Speed"

  project "Modme"
    kind "SharedLib"
    targetextension ".asi"

    files {
      "src/**"
    }

    files(minhook)

    includedirs {
      "vendor/minhook/include"
    }

  project "TestDependency"
    kind "SharedLib"
    targetdir "build/bin/%{cfg.buildcfg}/dependency"

    files {
      "test/plugin/dependency.cpp"
    }

  project "TestDependent"
    kind "SharedLib"
    targetextension ".asi"

    links {
      "TestDependency"
    }

    files {
      "test/plugin/dependent.cpp"
    }

  project "TestModme"
    kind "ConsoleApp"

    links {
      "version"
    }

    files {
      "src/log.*",
      "src/mods.*",
      "src/hooks.*",
      "test/modme/**"
    }

    files(minhook)

    dependson {
      "Modme",
      "TestDependent",
      "TestPlugin",
    }

    includedirs {
      "src",
      "vendor/doctest/doctest",
      "vendor/minhook/include"
    }

  project "TestPlugin"
    kind "SharedLib"
    targetextension ".asi"

    files {
      "test/plugin/plugin.cpp"
    }
