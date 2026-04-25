#include <catch2/catch_test_macros.hpp>
#include <campello/core/world.hpp>

using namespace campello::core;

struct Time {
    float delta = 0.016f;
    float elapsed = 0.0f;
};

static int destructor_call_count = 0;

struct TrackedResource {
    int value = 0;
    TrackedResource() = default;
    explicit TrackedResource(int v) : value(v) {}
    ~TrackedResource() { destructor_call_count++; }
};

TEST_CASE("Resource init and get") {
    World world;
    Time& t = world.init_resource<Time>(0.033f, 1.0f);
    REQUIRE(t.delta == 0.033f);
    REQUIRE(t.elapsed == 1.0f);

    // Verify the resource is accessible via World API
    REQUIRE(world.resource<Time>().delta == 0.033f);
}

TEST_CASE("Resource auto-init") {
    World world;
    Time& t = world.resource<Time>();
    REQUIRE(t.delta == 0.016f); // default constructed with member initializer
}

TEST_CASE("Res and ResMut access") {
    World world;
    world.init_resource<Time>(0.016f, 0.0f);

    {
        auto r = world.res<Time>();
        REQUIRE(r->delta == 0.016f);
    } // immutable borrow ends

    {
        auto rm = world.res_mut<Time>();
        rm->delta = 0.033f;
    } // mutable borrow ends

    REQUIRE(world.resource<Time>().delta == 0.033f);
}

// Phase 1: Resource safety tests -----------------------------------------------

TEST_CASE("Resource destructor is called on world destruction") {
    destructor_call_count = 0;
    {
        World world;
        world.init_resource<TrackedResource>(42);
        REQUIRE(destructor_call_count == 0);
    }
    REQUIRE(destructor_call_count == 1);
}

TEST_CASE("Resource multiple instances have independent destructors") {
    destructor_call_count = 0;
    {
        World world;
        world.init_resource<TrackedResource>(1);
        world.init_resource<Time>(0.0f, 0.0f); // unrelated resource
        world.init_resource<TrackedResource>(2); // second TrackedResource? No, same type — returns same instance
        // init_resource<T> for existing T returns the existing one
    }
    // Only one TrackedResource was created, so destructor called once
    REQUIRE(destructor_call_count == 1);
}

TEST_CASE("Resource sequential mutable borrows are allowed") {
    World world;
    world.init_resource<Time>(0.016f, 0.0f);

    {
        auto rm1 = world.res_mut<Time>();
        rm1->delta = 0.1f;
    } // borrow ends here

    {
        auto rm2 = world.res_mut<Time>();
        REQUIRE(rm2->delta == 0.1f);
        rm2->delta = 0.2f;
    } // borrow ends here

    REQUIRE(world.resource<Time>().delta == 0.2f);
}

TEST_CASE("Resource sequential immutable then mutable borrows are allowed") {
    World world;
    world.init_resource<Time>(0.016f, 0.0f);

    {
        auto r = world.res<Time>();
        REQUIRE(r->delta == 0.016f);
    } // immutable borrow ends

    {
        auto rm = world.res_mut<Time>();
        rm->delta = 0.033f;
    } // mutable borrow ends

    REQUIRE(world.resource<Time>().delta == 0.033f);
}
