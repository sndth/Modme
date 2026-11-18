local minhook = {
  "vendor/minhook/src/*.c",
  "vendor/minhook/src/*.h",
  "vendor/minhook/include/*.h",
  "vendor/minhook/src/hde/hde32.*",
  "vendor/minhook/src/hde/hde64.*",
  "vendor/minhook/src/hde/pstdint.h",
  "vendor/minhook/src/hde/table*.h"
}

workspace "Modme"
  symbols "On"
  location "build"
  cppdialect "C++23"
  platforms { "x86", "x64" }
  defaultplatform "x86"
  targetdir "build/bin/%{cfg.platform}/%{cfg.buildcfg}"
  objdir "build/obj/%{cfg.platform}/%{cfg.buildcfg}/%{prj.name}"
  startproject "TestModme"
  staticruntime "On"

  defines {
    "NOMINMAX"
  }

  buildoptions {
    "/utf-8"
  }

  configurations {
    "Debug",
    "Release",
    "ASan"
  }

  vpaths {
    ["*"] = {
      "src/**",
      "test/plugin/**"
    },
    ["Tests/*"] = "test/modme/**",
    ["MinHook/*"] = "vendor/minhook/**"
  }

  filter "platforms:x86"
    architecture "x86"

  filter "platforms:x64"
    architecture "x86_64"

  filter "configurations:Release"
    optimize "Speed"

  filter "configurations:ASan"
    staticruntime "Off"
    runtime "Release"
    sanitize { "Address" }
    runtimechecks "Off"
    editandcontinue "Off"
    incrementallink "Off"

  filter {}

  project "Modme"
    kind "SharedLib"
    targetextension ".asi"

    files {
      "src/**"
    }

    files(minhook)

    includedirs {
      "vendor/minhook/include",
      "vendor/fkYAML/single_include"
    }

  project "TestDependency"
    kind "SharedLib"
    targetdir "build/bin/%{cfg.platform}/%{cfg.buildcfg}/dependency"

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
      "src/config.*",
      "src/archive.*",
      "test/modme/**",
      "src/archive_hooks.*"
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
      "vendor/minhook/include",
      "vendor/fkYAML/single_include"
    }

  project "TestPlugin"
    kind "SharedLib"
    targetextension ".asi"

    files {
      "test/plugin/plugin.cpp"
    }
