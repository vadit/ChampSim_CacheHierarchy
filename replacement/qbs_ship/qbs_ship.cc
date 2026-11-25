#include "qbs_ship.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>

#include "champsim.h"

// initialize replacement state (identical to ship)
qbs_ship::qbs_ship(CACHE* cache)
    : replacement(cache), NUM_SET(cache->NUM_SET), NUM_WAY(cache->NUM_WAY), MAX_QUERIES(cache->NUM_WAY),
      sampler(champsim::msl::get_num_samples(NUM_SET) * NUM_CPUS * static_cast<std::size_t>(NUM_WAY)),
      rrpv_values(static_cast<std::size_t>(NUM_SET * NUM_WAY), maxRRPV), set_categorizer(champsim::msl::get_sample_rate(NUM_SET)),
      sampler_tag_bits(cache->OFFSET_BITS)
{
  std::generate_n(std::back_inserter(SHCT), NUM_CPUS, []() -> typename decltype(SHCT)::value_type { return {}; });
}

int& qbs_ship::get_rrpv(long set, long way) { return rrpv_values.at(static_cast<std::size_t>(set * NUM_WAY + way)); }

long qbs_ship::ship_victim(long set)
{
  // look for the maxRRPV line (identical to ship::find_victim)
  auto begin = std::next(std::begin(rrpv_values), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);

  auto victim = std::max_element(begin, end);
  if (auto rrpv_update = maxRRPV - *victim; rrpv_update != 0)
    for (auto it = begin; it != end; ++it)
      *it += rrpv_update;

  assert(begin <= victim);
  assert(victim < end);
  return std::distance(begin, victim);
}

long qbs_ship::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                           champsim::address full_addr, access_type type)
{
  long victim = ship_victim(set);
  if (intern_->upper_caches.empty())
    return victim; // nothing to query (e.g. a first-level cache)

  ++victim_selections;
  for (long q = 0; q < MAX_QUERIES; ++q) {
    ++queries;
    if (!current_set[victim].valid || !intern_->upper_holds_block(current_set[victim].address))
      return victim;

    // Resident above: treat the query as a hit for replacement (not for training) and try the next candidate
    ++spared;
    get_rrpv(set, victim) = 0;
    victim = ship_victim(set);
  }

  ++query_limit_hit;
  return victim;
}

// called on every cache hit and cache fill (identical to ship)
void qbs_ship::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                        champsim::address victim_addr, access_type type, uint8_t hit)
{
  using namespace champsim::data::data_literals;

  // update sampler
  if (set_categorizer.get_sample_category(set) == 0) {
    auto s_idx = set / champsim::msl::get_sample_rate(NUM_SET);
    auto s_set_begin = std::next(std::begin(sampler), s_idx * NUM_WAY + champsim::msl::get_num_samples(NUM_SET) * NUM_WAY * triggering_cpu);
    auto s_set_end = std::next(s_set_begin, NUM_WAY);

    // check hit
    auto match = std::find_if(s_set_begin, s_set_end, [addr = full_addr, shamt = sampler_tag_bits](auto x) {
      return x.valid && x.address.slice_upper(shamt) == addr.slice_upper(shamt);
    });
    if (match != s_set_end) {
      auto SHCT_idx = match->ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;
      SHCT[triggering_cpu][SHCT_idx] -= 1;

      match->used = true;
    } else {
      match = std::min_element(s_set_begin, s_set_end, [](auto x, auto y) { return x.last_used < y.last_used; });

      if (!match->used) {
        auto SHCT_idx = match->ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;
        SHCT[triggering_cpu][SHCT_idx] += 1;
      }

      match->valid = true;
      match->address = full_addr;
      match->ip = ip;
      match->used = false;
    }

    // update LRU state
    match->last_used = access_count++;
  }

  if (hit)
    get_rrpv(set, way) = 0;
}

// identical to ship
void qbs_ship::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                      champsim::address victim_addr, access_type type)
{
  // handle writeback access
  if (access_type{type} == access_type::WRITE) {
    get_rrpv(set, way) = maxRRPV - 1;
    return;
  }

  using namespace champsim::data::data_literals;
  // SHIP prediction
  auto SHCT_idx = ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;

  get_rrpv(set, way) = maxRRPV - 1;
  if (SHCT[triggering_cpu][SHCT_idx].is_max())
    get_rrpv(set, way) = maxRRPV;
}

void qbs_ship::replacement_final_stats()
{
  fmt::print("{} QBS-SHIP SELECTIONS: {} QUERIES: {} SPARED: {} QUERY LIMIT HIT: {}\n", intern_->NAME, victim_selections, queries, spared, query_limit_hit);
}
