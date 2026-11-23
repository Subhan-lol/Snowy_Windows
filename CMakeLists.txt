cmake_minimum_required(VERSION 3.16)
project(Snowy LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(Snowy WIN32
  src/main.cpp
  src/util.cpp
  src/settings.cpp
  src/renderer.cpp
  src/tracker.cpp
  src/world.cpp
  src/snowman.cpp
  src/engine.cpp
  src/capture.cpp
  src/ui.cpp
  src/settings_ui.cpp
  res/snowy.rc
)

target_compile_definitions(Snowy PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Snowy PRIVATE
  d3d11 dxgi dwmapi shcore comctl32 ole32 uuid windowscodecs gdi32 user32 shell32 advapi32 winmm wtsapi32 msimg32)

if(MSVC)
  target_compile_options(Snowy PRIVATE /W3 /utf-8 /MP)
  # Static CRT: a single self-contained exe.
  set_property(TARGET Snowy PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # The manifest is embedded from the .rc file.
  target_link_options(Snowy PRIVATE /MANIFEST:NO)
else()
  target_compile_options(Snowy PRIVATE -Wall -Wno-unknown-pragmas -Wno-missing-field-initializers)
  target_link_options(Snowy PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
