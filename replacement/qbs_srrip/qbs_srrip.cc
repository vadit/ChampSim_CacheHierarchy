#include "qbs_srrip.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>

qbs_srrip::qbs_srrip(CACHE* cache)
    : replacement(cache), NUM_WAY(cache->NUM_WAY), MAX_QUERIES(cache->NUM_WAY), rrpv(static_cast<std::size_t>(cache->NUM_SET * cache->NUM_WAY), maxRRPV)
{
}

int& qbs_srrip::get_rrpv(long set, long way) { return rrpv.at(static_cast<std::size_t>(set * NUM_WAY + way)); }

long qbs_srrip::srrip_victim(long set)
{
  auto begin = std::next(std::begin(rrpv), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);

  // Find the maximum RRPV, and age every line so that it reaches maxRRPV
  auto victim = std::max_element(begin, end);
  int diff = maxRRPV - *victim;
  std::transform(begin, end, begin, [diff](int x) { return x + diff; });

  return std::distance(begin, victim);
}

long qbs_srrip::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                            champsim::address full_addr, access_type type)
{
  long victim = srrip_victim(set);
  if (intern_->upper_caches.empty())
    return victim; // nothing to query (e.g. a first-level cache)

  ++victim_selections;
  for (long q = 0; q < MAX_QUERIES; ++q) {
    ++queries;
    if (!current_set[victim].valid || !intern_->upper_holds_block(current_set[victim].address))
      return victim;

    // Resident above: treat the query as a hit and try the next candidate
    ++spared;
    get_rrpv(set, victim) = 0;
    victim = srrip_victim(set);
  }

  ++query_limit_hit;
  return victim;
}

void qbs_srrip::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                         champsim::address victim_addr, access_type type, uint8_t hit)
{
  // Same as SRRIP: called on hits and fills
  if (way < NUM_WAY)
    get_rrpv(set, way) = hit ? 0 : (maxRRPV - 1);
}

void qbs_srrip::replacement_final_stats()
{
  fmt::print("{} QBS-SRRIP SELECTIONS: {} QUERIES: {} SPARED: {} QUERY LIMIT HIT: {}\n", intern_->NAME, victim_selections, queries, spared, query_limit_hit);
}
