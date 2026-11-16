#include <catch.hpp>

#include "../replacement/qbs_lru/qbs_lru.h"
#include "../replacement/qbs_srrip/qbs_srrip.h"
#include "defaults.hpp"
#include "mocks.hpp"

namespace
{
constexpr long num_ways = 4;

template <typename P>
P& get_policy(CACHE& cache)
{
  auto* model = dynamic_cast<CACHE::replacement_module_model<P>*>(cache.repl_module_pimpl.get());
  REQUIRE(model != nullptr);
  return std::get<P>(model->intern_);
}

champsim::address block_addr(long i) { return champsim::address{static_cast<uint64_t>(0x10000 + i * 0x1000)}; }

std::vector<champsim::cache_block> full_set()
{
  std::vector<champsim::cache_block> blocks(num_ways);
  for (long i = 0; i < num_ways; ++i) {
    blocks[static_cast<std::size_t>(i)].valid = true;
    blocks[static_cast<std::size_t>(i)].address = block_addr(i);
  }
  return blocks;
}

// Fill ways 0..3 in order, so way 0 is the LRU (and the first max-RRPV) candidate
template <typename P>
void fill_in_order(P& uut)
{
  for (long i = 0; i < num_ways; ++i) {
    if constexpr (std::is_same_v<P, qbs_lru>)
      uut.replacement_cache_fill(0, 0, i, block_addr(i), champsim::address{}, champsim::address{}, access_type::LOAD);
    else
      uut.update_replacement_state(0, 0, i, block_addr(i), champsim::address{}, champsim::address{}, access_type::LOAD, 0);
  }
}

void make_resident(CACHE& upper, std::initializer_list<long> indices)
{
  std::size_t slot = 0;
  for (auto i : indices) {
    upper.block.at(slot).valid = true;
    upper.block.at(slot).address = block_addr(i);
    ++slot;
  }
}
} // namespace

TEMPLATE_TEST_CASE("QBS skips victims that are resident in an upper-level cache", "", qbs_lru, qbs_srrip)
{
  do_nothing_MRC mock_ll;
  CACHE upper{champsim::cache_builder{champsim::defaults::default_l2c}.name("456-upper").sets(1).ways(num_ways).lower_level(&mock_ll.queues)};
  CACHE lower{champsim::cache_builder{champsim::defaults::default_llc}
                  .name("456-lower")
                  .sets(1)
                  .ways(num_ways)
                  .lower_level(&mock_ll.queues)
                  .set_inclusive()
                  .template replacement<TestType>()};
  auto& uut = get_policy<TestType>(lower);
  auto blocks = full_set();
  fill_in_order(uut);

  SECTION("With no upper caches, the base policy's victim is chosen")
  {
    CHECK(uut.find_victim(0, 0, 0, blocks.data(), champsim::address{}, champsim::address{}, access_type::LOAD) == 0);
  }

  SECTION("A resident candidate is spared, and the next candidate is chosen")
  {
    lower.upper_caches = {&upper};
    make_resident(upper, {0});
    CHECK(uut.find_victim(0, 0, 0, blocks.data(), champsim::address{}, champsim::address{}, access_type::LOAD) == 1);
  }

  SECTION("Several resident candidates are skipped in order")
  {
    lower.upper_caches = {&upper};
    make_resident(upper, {0, 1, 2});
    CHECK(uut.find_victim(0, 0, 0, blocks.data(), champsim::address{}, champsim::address{}, access_type::LOAD) == 3);
  }

  SECTION("If every candidate is resident, a valid way is still returned")
  {
    lower.upper_caches = {&upper};
    make_resident(upper, {0, 1, 2, 3});
    auto victim = uut.find_victim(0, 0, 0, blocks.data(), champsim::address{}, champsim::address{}, access_type::LOAD);
    CHECK(victim >= 0);
    CHECK(victim < num_ways);
  }
}

TEST_CASE("The residency query looks through every upper level")
{
  do_nothing_MRC mock_ll;
  CACHE l1{champsim::cache_builder{champsim::defaults::default_l1d}.name("456-l1").sets(1).ways(num_ways).lower_level(&mock_ll.queues)};
  CACHE l2{champsim::cache_builder{champsim::defaults::default_l2c}.name("456-l2").sets(1).ways(num_ways).lower_level(&mock_ll.queues)};
  CACHE llc{champsim::cache_builder{champsim::defaults::default_llc}.name("456-llc").sets(1).ways(num_ways).lower_level(&mock_ll.queues)};
  llc.upper_caches = {&l2};
  l2.upper_caches = {&l1};

  make_resident(l1, {2});

  CHECK(llc.upper_holds_block(block_addr(2)));
  CHECK_FALSE(llc.upper_holds_block(block_addr(1)));
}
