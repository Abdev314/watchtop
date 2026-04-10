cmake_minimum_required(VERSION 3.16)
project(watchtop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch FTXUI automatically
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

add_executable(watchtop
  src/main.cpp
  src/system/SystemLimits.cpp
  src/scanner/ProcScanner.cpp
  src/scanner/InotifyParser.cpp
  src/scanner/WatchCollector.cpp
  src/ui/Dashboard.cpp
  src/ui/ProcessTable.cpp
  src/ui/WatchDetails.cpp
  src/ui/CommandPanel.cpp
  src/scanner/PortScanner.cpp
)
  

target_include_directories(watchtop PRIVATE src)
target_link_libraries(watchtop PRIVATE ftxui::screen ftxui::dom ftxui::component)