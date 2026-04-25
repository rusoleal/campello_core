cmake_minimum_required(VERSION 3.20)

# Fetch nanobench
include(FetchContent)
FetchContent_Declare(
    nanobench
    GIT_REPOSITORY https://github.com/martinus/nanobench.git
    GIT_TAG v4.3.11
)
FetchContent_MakeAvailable(nanobench)

set(BM_SOURCES
    bm_iteration.cpp
    bm_spawn.cpp
    bm_query.cpp
    bm_schedule.cpp
    bm_memory.cpp
)

# One executable per benchmark file (avoids duplicate main symbols)
foreach(src ${BM_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE campello::core nanobench::nanobench)
endforeach()
