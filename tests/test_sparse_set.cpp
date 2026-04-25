#include <catch2/catch_test_macros.hpp>
#include <campello/core/detail/sparse_set.hpp>

using namespace campello::core::detail;

TEST_CASE("SparseSet basic operations") {
    SparseSet<std::uint32_t, int> set;
    set.emplace(5, 100);
    set.emplace(10, 200);
    set.emplace(10000, 300);

    REQUIRE(set.contains(5));
    REQUIRE(set.contains(10));
    REQUIRE(set.contains(10000));
    REQUIRE(!set.contains(7));

    REQUIRE(*set.get(5) == 100);
    REQUIRE(*set.get(10) == 200);
    REQUIRE(*set.get(10000) == 300);

    set.remove(10);
    REQUIRE(!set.contains(10));
    REQUIRE(set.size() == 2);
}

TEST_CASE("SparseSet tag set") {
    SparseSet<std::uint64_t, void> set;
    set.insert(1);
    set.insert(100);
    set.insert(100000);

    REQUIRE(set.contains(1));
    REQUIRE(set.contains(100));
    REQUIRE(!set.contains(2));
    REQUIRE(set.size() == 3);

    set.remove(100);
    REQUIRE(!set.contains(100));
    REQUIRE(set.size() == 2);
}

TEST_CASE("SparseSet iteration") {
    SparseSet<std::uint32_t, int> set;
    set.emplace(1, 10);
    set.emplace(5, 50);
    set.emplace(3, 30);

    int sum = 0;
    for (auto* it = set.values_begin(); it != set.values_end(); ++it) {
        sum += *it;
    }
    REQUIRE(sum == 90);
}
