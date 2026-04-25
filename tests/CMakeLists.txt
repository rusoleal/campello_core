cmake_minimum_required(VERSION 3.20)

# Fetch Catch2 v3
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

set(TEST_SOURCES
    test_main.cpp
    test_types.cpp
    test_allocator.cpp
    test_sparse_set.cpp
    test_world.cpp
    test_query.cpp
    test_commands.cpp
    test_event.cpp
    test_resource.cpp
    test_schedule.cpp
    test_hierarchy.cpp
    test_reflect.cpp
    test_serialize.cpp
    test_snapshot.cpp
    test_stress.cpp
    test_prefab.cpp
)

add_executable(campello_core_tests ${TEST_SOURCES})
target_link_libraries(campello_core_tests PRIVATE campello::core Catch2::Catch2WithMain)

catch_discover_tests(campello_core_tests)

# Cross-platform compile-only smoke test (Android, iOS, Windows ARM64, etc.)
add_executable(smoke_test android_smoke.cpp)
target_link_libraries(smoke_test PRIVATE campello::core)
