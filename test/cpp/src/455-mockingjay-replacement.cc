#include <catch.hpp>

#include "../replacement/mockingjay/mockingjay.h"
#include "defaults.hpp"

namespace
{
constexpr long num_ways = 16;

mockingjay& get_policy(CACHE& cache)
{
  auto* model = dynamic_cast<CACHE::replacement_module_model<mockingjay>*>(cache.repl_module_pimpl.get());
  REQUIRE(model != nullptr);
  return std::get<mockingjay>(model->intern_);
}

std::vector<champsim::cache_block> full_set()
{
  std::vector<champsim::cache_block> blocks(num_ways);
  for (auto& b : blocks)
    b.valid = true;
  return blocks;
}

// Fill many distinct blocks from one PC; none are ever reused, so the sampler detrains the PC towards INF_RD
void stream(mockingjay& uut, champsim::address ip, int count)
{
  for (int i = 0; i < count; ++i) {
    champsim::address addr{static_cast<uint64_t>(0x100000 + i * 64)};
    uut.replacement_cache_fill(0, 0, i % num_ways, addr, ip, champsim::address{}, access_type::LOAD);
  }
}

// Fill a few blocks from one PC and reuse them shortly afterwards
void reuse(mockingjay& uut, champsim::address ip, int rounds)
{
  for (int r = 0; r < rounds; ++r) {
    for (int i = 0; i < 4; ++i) {
      champsim::address addr{static_cast<uint64_t>(0x900000 + i * 64)};
      if (r == 0)
        uut.replacement_cache_fill(0, 0, i, addr, ip, champsim::address{}, access_type::LOAD);
      else
        uut.update_replacement_state(0, 0, i, addr, ip, champsim::address{}, access_type::LOAD, 1);
    }
  }
}
} // namespace

TEST_CASE("Mockingjay bypasses lines from a PC whose lines are never reused")
{
  CACHE cache{champsim::cache_builder{champsim::defaults::default_llc}.name("455-mj").sets(1).ways(num_ways).replacement<mockingjay>()};
  auto& uut = get_policy(cache);
  const champsim::address stream_ip{0x402000};
  auto blocks = full_set();

  stream(uut, stream_ip, 400);

  CHECK(uut.find_victim(0, 0, 0, blocks.data(), stream_ip, champsim::address{0xdead0000}, access_type::LOAD) == num_ways);
}

TEST_CASE("Mockingjay does not bypass in an inclusive cache")
{
  CACHE cache{
      champsim::cache_builder{champsim::defaults::default_llc}.name("455-mj-incl").sets(1).ways(num_ways).set_inclusive().replacement<mockingjay>()};
  auto& uut = get_policy(cache);
  const champsim::address stream_ip{0x402000};
  auto blocks = full_set();

  stream(uut, stream_ip, 400);

  auto victim = uut.find_victim(0, 0, 0, blocks.data(), stream_ip, champsim::address{0xdead0000}, access_type::LOAD);
  CHECK(victim >= 0);
  CHECK(victim < num_ways);
}

TEST_CASE("Mockingjay keeps lines from a PC with short reuse distances")
{
  CACHE cache{champsim::cache_builder{champsim::defaults::default_llc}.name("455-mj-reuse").sets(1).ways(num_ways).replacement<mockingjay>()};
  auto& uut = get_policy(cache);
  const champsim::address hot_ip{0x401000};
  auto blocks = full_set();

  reuse(uut, hot_ip, 50);

  CHECK(uut.find_victim(0, 0, 0, blocks.data(), hot_ip, champsim::address{0xdead0000}, access_type::LOAD) < num_ways);
}

TEST_CASE("Mockingjay never bypasses writebacks")
{
  CACHE cache{champsim::cache_builder{champsim::defaults::default_llc}.name("455-mj-wb").sets(1).ways(num_ways).replacement<mockingjay>()};
  auto& uut = get_policy(cache);
  const champsim::address stream_ip{0x402000};
  auto blocks = full_set();

  stream(uut, stream_ip, 400);

  CHECK(uut.find_victim(0, 0, 0, blocks.data(), stream_ip, champsim::address{0xdead0000}, access_type::WRITE) < num_ways);
}
