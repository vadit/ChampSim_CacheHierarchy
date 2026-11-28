#include <catch.hpp>

#include "cache.h"
#include "defaults.hpp"
#include "hierarchy_view.h"
#include "mocks.hpp"

namespace
{
struct view_hierarchy {
  do_nothing_MRC mock_ll;
  champsim::channel between{32, 32, 32, champsim::data::bits{LOG2_BLOCK_SIZE}, false};
  to_rq_MRP mock_ul_read;
  to_wq_MRP mock_ul_write;

  CACHE upper{champsim::cache_builder{champsim::defaults::default_l2c}
                  .name("457-upper")
                  .sets(1)
                  .ways(1)
                  .upper_levels({{&mock_ul_read.queues, &mock_ul_write.queues}})
                  .lower_level(&between)
                  .hit_latency(2)
                  .fill_latency(2)};
  CACHE lower{champsim::cache_builder{champsim::defaults::default_llc}
                  .name("457-lower")
                  .sets(1)
                  .ways(4)
                  .upper_levels({&between})
                  .lower_level(&mock_ll.queues)
                  .hit_latency(2)
                  .fill_latency(2)
                  .set_inclusive()};

  std::array<champsim::operable*, 5> elements{{&upper, &lower, &mock_ll, &mock_ul_read, &mock_ul_write}};
  champsim::hierarchy_view view{&lower};

  view_hierarchy()
  {
    lower.upper_caches = {&upper};
    upper.lower_cache = &lower;
    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }
  }

  void run()
  {
    for (int i = 0; i < 50; ++i)
      for (auto elem : elements)
        elem->_operate();
  }

  void load(champsim::address addr, uint64_t id)
  {
    decltype(mock_ul_read)::request_type pkt;
    pkt.address = addr;
    pkt.cpu = 0;
    pkt.type = access_type::LOAD;
    pkt.instr_id = id;
    REQUIRE(mock_ul_read.issue(pkt));
    run();
  }

  void writeback(champsim::address addr, uint64_t id)
  {
    decltype(mock_ul_write)::request_type pkt;
    pkt.address = addr;
    pkt.cpu = 0;
    pkt.type = access_type::WRITE;
    pkt.instr_id = id;
    REQUIRE(mock_ul_write.issue(pkt));
    run();
  }
};
} // namespace

SCENARIO("The hierarchy view reports upper-level residency and reuse")
{
  GIVEN("A 1-way upper cache above an inclusive lower cache")
  {
    view_hierarchy h;
    const champsim::address a{0xdeadbe40};
    const champsim::address b{0xcafeba80};

    THEN("Nothing is resident before any access")
    {
      CHECK(h.view.has_upper_levels());
      CHECK_FALSE(h.view.upper_resident(a));
      CHECK_FALSE(h.view.upper_rereferenced(a));
    }

    WHEN("Block A is loaded once")
    {
      h.load(a, 1);

      THEN("A is resident above but has not been re-referenced")
      {
        CHECK(h.view.upper_resident(a));
        CHECK_FALSE(h.view.upper_rereferenced(a));
      }

      AND_WHEN("A is loaded again, hitting in the upper level")
      {
        h.load(a, 2);
        THEN("A is reported as re-referenced") { CHECK(h.view.upper_rereferenced(a)); }

        AND_WHEN("B replaces A in the upper level and A is loaded back")
        {
          h.load(b, 3);
          REQUIRE_FALSE(h.view.upper_resident(a));
          h.load(a, 4);
          THEN("The refilled copy starts without reuse") { CHECK_FALSE(h.view.upper_rereferenced(a)); }
        }
      }

      AND_WHEN("A receives only a writeback hit")
      {
        h.writeback(a, 2);
        THEN("A writeback does not count as a re-reference") { CHECK_FALSE(h.view.upper_rereferenced(a)); }
      }
    }
  }
}

TEST_CASE("A cache with no upper levels has an empty hierarchy view")
{
  do_nothing_MRC mock_ll;
  CACHE l1{champsim::cache_builder{champsim::defaults::default_l1d}.name("457-l1").sets(1).ways(1).lower_level(&mock_ll.queues)};
  champsim::hierarchy_view view{&l1};
  CHECK_FALSE(view.has_upper_levels());
  CHECK_FALSE(view.upper_resident(champsim::address{0x1000}));
  CHECK_FALSE(view.upper_rereferenced(champsim::address{0x1000}));
}
