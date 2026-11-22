cmake_minimum_required(VERSION 3.16)
project(watchtop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # needed for a PIE (ASLR-friendly) binary

# Build optimized by default when the user does not choose a build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# FTXUI is downloaded at build time. Skip its examples, docs and tests.
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0      
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ftxui)

add_executable(watchtop
  src/main.cpp
  src/system/SystemLimits.cpp
  src/scanner/ProcScanner.cpp
  src/scanner/InotifyParser.cpp
  src/scanner/WatchCollector.cpp
  src/scanner/LeakDetector.cpp
  src/ui/Dashboard.cpp
  src/ui/ProcessTable.cpp
  src/ui/WatchDetails.cpp
  src/ui/HistoryTracker.cpp
  src/system/ProcessControl.cpp
)

target_include_directories(watchtop PRIVATE src)
target_link_libraries(watchtop PRIVATE ftxui::screen ftxui::dom ftxui::component)
target_compile_definitions(watchtop PRIVATE WATCHTOP_VERSION="${PROJECT_VERSION}")

# Warnings and security hardening for our own code.
target_compile_options(watchtop PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow
  -fstack-protector-strong
  -fstack-clash-protection
  $<$<CONFIG:Release>:-U_FORTIFY_SOURCE>
  $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i[3-6]86")
  target_compile_options(watchtop PRIVATE -fcf-protection)
endif()

# Resolve all symbols at start-up and make relocation tables read-only.
target_link_options(watchtop PRIVATE -Wl,-z,relro,-z,now)

install(TARGETS watchtop RUNTIME DESTINATION bin)