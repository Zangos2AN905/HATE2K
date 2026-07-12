cmake_minimum_required(VERSION 3.21)
project(HATE2K LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ── Slint UI ────────────────────────────────────────────────────────────
FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG release/1
    SOURCE_SUBDIR api/cpp
)

# ── liblcf (RPG Maker 2000/2003 LCF parser) ───────────────────────────
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LIBLCF_WITH_INI OFF CACHE BOOL "" FORCE)
set(LIBLCF_WITH_ICU OFF CACHE BOOL "" FORCE)
set(LIBLCF_WITH_XML OFF CACHE BOOL "" FORCE)
set(LIBLCF_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(LIBLCF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    liblcf
    GIT_REPOSITORY https://github.com/EasyRPG/liblcf.git
    GIT_TAG master
)

FetchContent_MakeAvailable(Slint liblcf)

add_executable(hate2k
    src/main.cpp
    src/corruptor.cpp
    src/dialogue.cpp
)

slint_target_sources(hate2k src/ui/app.slint)
target_link_libraries(hate2k PRIVATE Slint::Slint lcf opengl32 imm32)
