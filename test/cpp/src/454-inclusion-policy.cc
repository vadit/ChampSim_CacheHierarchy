#include <catch.hpp>

#include "cache.h"
#include "defaults.hpp"
#include "mocks.hpp"

namespace
{
bool holds(const CACHE& cache, champsim::address addr)
{
  return std::any_of(std::begin(cache.block), std::end(cache.block), [match = addr.slice_upper(cache.OFFSET_BITS), bits = cache.OFFSET_BITS](const auto& blk) {
    return blk.valid && blk.address.slice_upper(bits) == match;
  });
}

bool holds_dirty(const CACHE& cache, champsim::address addr)
{
  return std::any_of(std::begin(cache.block), std::end(cache.block), [match = addr.slice_upper(cache.OFFSET_BITS), bits = cache.OFFSET_BITS](const auto& blk) {
    return blk.valid && blk.dirty && blk.address.slice_upper(bits) == match;
  });
}

struct two_level_hierarchy {
  static constexpr uint64_t hit_latency = 2;
  static constexpr uint64_t fill_latency = 2;

  do_nothing_MRC mock_ll;
  champsim::channel between{32, 32, 32, champsim::data::bits{LOG2_BLOCK_SIZE}, false};
  to_rq_MRP mock_ul_read;
  to_wq_MRP mock_ul_write;

  CACHE upper;
  CACHE lower;

  std::array<champsim::operable*, 5> elements{{&upper, &lower, &mock_ll, &mock_ul_read, &mock_ul_write}};

  // By default, a small upper cache that can hold both blocks, and a 1-way lower cache that can hold only one
  explicit two_level_hierarchy(champsim::inclusion_policy policy, uint32_t upper_ways = 4, uint32_t lower_ways = 1)
      : upper{champsim::cache_builder{champsim::defaults::default_l2c}
                  .name("454-upper")
                  .sets(1)
                  .ways(upper_ways)
                  .upper_levels({{&mock_ul_read.queues, &mock_ul_write.queues}})
                  .lower_level(&between)
                  .hit_latency(hit_latency)
                  .fill_latency(fill_latency)},
        lower{champsim::cache_builder{champsim::defaults::default_llc}
                  .name("454-lower")
                  .sets(1)
                  .ways(lower_ways)
                  .upper_levels({&between})
                  .lower_level(&mock_ll.queues)
                  .hit_latency(hit_latency)
                  .fill_latency(fill_latency)
                  .inclusion(policy)}
  {
    lower.upper_caches = {&upper};
    upper.lower_cache = &lower;
    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }
    run(10);
  }

  void run(int cycles)
  {
    for (int i = 0; i < cycles; ++i)
      for (auto elem : elements)
        elem->_operate();
  }

  bool load(champsim::address addr, uint64_t id)
  {
    decltype(mock_ul_read)::request_type pkt;
    pkt.address = addr;
    pkt.cpu = 0;
    pkt.type = access_type::LOAD;
    pkt.instr_id = id;
    auto result = mock_ul_read.issue(pkt);
    run(50);
    return result;
  }

  bool writeback(champsim::address addr, uint64_t id)
  {
    decltype(mock_ul_write)::request_type pkt;
    pkt.address = addr;
    pkt.cpu = 0;
    pkt.type = access_type::WRITE;
    pkt.instr_id = id;
    auto result = mock_ul_write.issue(pkt);
    run(50);
    return result;
  }
};
} // namespace

SCENARIO("The cache builder records the inclusion policy")
{
  do_nothing_MRC mock_ll;
  CACHE default_cache{champsim::cache_builder{champsim::defaults::default_llc}.name("454-default").lower_level(&mock_ll.queues)};
  CACHE inclusive_cache{champsim::cache_builder{champsim::defaults::default_llc}.name("454-incl").lower_level(&mock_ll.queues).set_inclusive()};

  CHECK(default_cache.inclusion == champsim::inclusion_policy::non_inclusive);
  CHECK(default_cache.bypass_allowed());
  CHECK(inclusive_cache.inclusion == champsim::inclusion_policy::inclusive);
  CHECK_FALSE(inclusive_cache.bypass_allowed());
}

SCENARIO("An inclusive cache back-invalidates upper levels when it evicts")
{
  GIVEN("An upper cache above a 1-way inclusive lower cache")
  {
    two_level_hierarchy h{champsim::inclusion_policy::inclusive};
    const champsim::address a{0xdeadbe40};
    const champsim::address b{0xcafeba80};

    WHEN("Block A is loaded")
    {
      REQUIRE(h.load(a, 1));

      THEN("Both levels hold A")
      {
        CHECK(holds(h.upper, a));
        CHECK(holds(h.lower, a));
      }

      AND_WHEN("Block B is loaded, evicting A from the lower level")
      {
        REQUIRE(h.load(b, 2));

        THEN("A is invalidated in the upper level as well")
        {
          CHECK(holds(h.lower, b));
          CHECK(holds(h.upper, b));
          CHECK_FALSE(holds(h.lower, a));
          CHECK_FALSE(holds(h.upper, a));
          CHECK(h.lower.sim_stats.back_inval_issued == 1);
          CHECK(h.upper.sim_stats.back_inval_received == 1);
        }

        THEN("A clean block is not written back") { CHECK(h.lower.sim_stats.back_inval_dirty == 0); }
      }
    }

    WHEN("Block A is loaded and then dirtied in the upper level only")
    {
      REQUIRE(h.load(a, 1));
      REQUIRE(h.writeback(a, 2));
      REQUIRE(holds_dirty(h.upper, a));
      REQUIRE_FALSE(holds_dirty(h.lower, a));

      AND_WHEN("Block B is loaded, evicting A from the lower level")
      {
        REQUIRE(h.load(b, 3));

        THEN("The dirty upper copy causes A to be written back to memory")
        {
          CHECK_FALSE(holds(h.upper, a));
          CHECK(h.lower.sim_stats.back_inval_dirty == 1);
          CHECK(std::count(std::begin(h.mock_ll.addresses), std::end(h.mock_ll.addresses), a) == 2); // the original read, and the writeback
        }
      }
    }
  }
}

SCENARIO("A non-inclusive cache does not affect upper levels when it evicts")
{
  GIVEN("An upper cache above a 1-way non-inclusive lower cache")
  {
    two_level_hierarchy h{champsim::inclusion_policy::non_inclusive};
    const champsim::address a{0xdeadbe40};
    const champsim::address b{0xcafeba80};

    WHEN("Block A then block B are loaded")
    {
      REQUIRE(h.load(a, 1));
      REQUIRE(h.load(b, 2));

      THEN("The upper level keeps A even though the lower level evicted it")
      {
        CHECK_FALSE(holds(h.lower, a));
        CHECK(holds(h.upper, a));
        CHECK(holds(h.upper, b));
        CHECK(h.lower.sim_stats.back_inval_issued == 0);
        CHECK(h.upper.sim_stats.back_inval_received == 0);
      }
    }
  }
}

namespace
{
// A replacement policy that always tries to bypass
struct always_bypass : champsim::modules::replacement {
  using replacement::replacement;
  long find_victim(uint32_t, uint64_t, long, const champsim::cache_block*, champsim::address, champsim::address, access_type) { return intern_->NUM_WAY; }
  void update_replacement_state(uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type, uint8_t) {}
};
} // namespace

SCENARIO("An inclusive cache does not allow fills to bypass")
{
  auto policy = GENERATE(champsim::inclusion_policy::non_inclusive, champsim::inclusion_policy::inclusive);
  GIVEN("A full 1-way cache whose replacement policy always bypasses")
  {
    do_nothing_MRC mock_ll;
    to_rq_MRP mock_ul;
    CACHE uut{champsim::cache_builder{champsim::defaults::default_llc}
                  .name("454-bypass")
                  .sets(1)
                  .ways(1)
                  .upper_levels({&mock_ul.queues})
                  .lower_level(&mock_ll.queues)
                  .hit_latency(2)
                  .fill_latency(2)
                  .inclusion(policy)
                  .replacement<always_bypass>()};
    std::array<champsim::operable*, 3> elements{{&uut, &mock_ll, &mock_ul}};
    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }

    auto issue = [&](champsim::address addr, uint64_t id) {
      decltype(mock_ul)::request_type pkt;
      pkt.address = addr;
      pkt.cpu = 0;
      pkt.type = access_type::LOAD;
      pkt.instr_id = id;
      mock_ul.issue(pkt);
      for (int i = 0; i < 50; ++i)
        for (auto elem : elements)
          elem->_operate();
    };

    const champsim::address a{0xdeadbe40};
    const champsim::address b{0xcafeba80};
    issue(a, 1); // fills the invalid way
    issue(b, 2); // the policy asks to bypass

    THEN("The second block bypasses only if the cache is non-inclusive")
    {
      if (policy == champsim::inclusion_policy::inclusive)
        CHECK(holds(uut, b));
      else
        CHECK(holds(uut, a));
    }
  }
}

SCENARIO("An exclusive cache holds only blocks that the upper level does not")
{
  GIVEN("A 1-way upper cache above a 4-way exclusive lower cache")
  {
    two_level_hierarchy h{champsim::inclusion_policy::exclusive, 1, 4};
    const champsim::address a{0xdeadbe40};
    const champsim::address b{0xcafeba80};

    WHEN("Block A is loaded")
    {
      REQUIRE(h.load(a, 1));

      THEN("Only the upper level holds A")
      {
        CHECK(holds(h.upper, a));
        CHECK_FALSE(holds(h.lower, a));
        CHECK(h.lower.sim_stats.excl_fills_skipped == 1);
      }

      AND_WHEN("Block B is loaded, evicting clean A from the upper level")
      {
        REQUIRE(h.load(b, 2));

        THEN("The clean victim A moves down to the lower level, and B is only in the upper level")
        {
          CHECK(holds(h.upper, b));
          CHECK_FALSE(holds(h.upper, a));
          CHECK(holds(h.lower, a));
          CHECK_FALSE(holds_dirty(h.lower, a));
          CHECK_FALSE(holds(h.lower, b));
          CHECK(h.lower.sim_stats.clean_victims_received == 1);
        }

        THEN("The clean victim is not sent to memory")
        {
          CHECK(std::count(std::begin(h.mock_ll.addresses), std::end(h.mock_ll.addresses), a) == 1);
        }

        AND_WHEN("Block A is loaded again")
        {
          REQUIRE(h.load(a, 3));

          THEN("A hits in the lower level and moves up, and B moves down")
          {
            CHECK(holds(h.upper, a));
            CHECK_FALSE(holds(h.lower, a));
            CHECK(holds(h.lower, b));
            CHECK(h.lower.sim_stats.excl_promotions == 1);
            CHECK(std::count(std::begin(h.mock_ll.addresses), std::end(h.mock_ll.addresses), a) == 1); // no second memory read
          }
        }
      }
    }

    WHEN("Block A is loaded, dirtied in the upper level, and evicted by B")
    {
      REQUIRE(h.load(a, 1));
      REQUIRE(h.writeback(a, 2));
      REQUIRE(holds_dirty(h.upper, a));
      REQUIRE(h.load(b, 3));

      THEN("The lower level holds A dirty") { CHECK(holds_dirty(h.lower, a)); }

      AND_WHEN("Block A is loaded again")
      {
        REQUIRE(h.load(a, 4));

        THEN("Ownership of the dirty block moves up with it, without a writeback to memory")
        {
          CHECK(holds_dirty(h.upper, a));
          CHECK_FALSE(holds(h.lower, a));
          CHECK(std::count(std::begin(h.mock_ll.addresses), std::end(h.mock_ll.addresses), a) == 1);
        }
      }
    }
  }
}

SCENARIO("An exclusive cache with no upper-level caches allocates normally")
{
  GIVEN("An exclusive cache whose only requester is not a cache")
  {
    do_nothing_MRC mock_ll;
    to_rq_MRP mock_ul;
    CACHE uut{champsim::cache_builder{champsim::defaults::default_l1d}
                  .name("454-excl-l1")
                  .sets(1)
                  .ways(2)
                  .upper_levels({&mock_ul.queues})
                  .lower_level(&mock_ll.queues)
                  .hit_latency(2)
                  .fill_latency(2)
                  .set_exclusive()};
    std::array<champsim::operable*, 3> elements{{&uut, &mock_ll, &mock_ul}};
    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }

    decltype(mock_ul)::request_type pkt;
    pkt.address = champsim::address{0xdeadbe40};
    pkt.cpu = 0;
    pkt.type = access_type::LOAD;
    pkt.instr_id = 1;
    mock_ul.issue(pkt);
    for (int i = 0; i < 50; ++i)
      for (auto elem : elements)
        elem->_operate();

    THEN("The block is filled") { CHECK(holds(uut, pkt.address)); }
  }
}
