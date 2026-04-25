#include <catch2/catch_test_macros.hpp>
#include <campello/core/detail/allocator.hpp>

using namespace campello::core::detail;

TEST_CASE("ArenaAllocator basic allocation") {
    ArenaAllocator arena(1024);
    void* p1 = arena.allocate(64, 8);
    REQUIRE(p1 != nullptr);
    void* p2 = arena.allocate(128, 16);
    REQUIRE(p2 != nullptr);
    REQUIRE(arena.allocated_bytes() >= 64 + 128);
}

TEST_CASE("ArenaAllocator construct") {
    ArenaAllocator arena;
    struct Foo { int x; float y; };
    Foo* f = arena.construct<Foo>(42, 3.14f);
    REQUIRE(f->x == 42);
    REQUIRE(f->y == 3.14f);
}

TEST_CASE("ArenaAllocator reset") {
    ArenaAllocator arena(1024);
    arena.allocate(512, 1);
    REQUIRE(arena.allocated_bytes() >= 512);
    arena.reset();
    REQUIRE(arena.allocated_bytes() == 0);
}
