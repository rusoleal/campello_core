cmake_minimum_required(VERSION 3.20)

set(EXAMPLES
    ex_01_hello_ecs
    ex_02_queries
    ex_03_systems
    ex_04_events
    ex_05_hierarchy
    ex_06_bulk_spawn
    ex_07_change_detection
    ex_08_thread_pool
    ex_09_save_load
    ex_10_demo_game
)

foreach(ex ${EXAMPLES})
    add_executable(${ex} ${ex}.cpp)
    target_link_libraries(${ex} PRIVATE campello::core)
endforeach()
