#include "qbs_lru.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>

qbs_lru::qbs_lru(CACHE* cache)
    : replacement(cache), NUM_WAY(cache->NUM_WAY), MAX_QUERIES(cache->NUM_WAY), last_used_cycles(static_cast<std::size_t>(cache->NUM_SET * cache->NUM_WAY), 0)
{
}

long qbs_lru::lru_way(long set) const
{
  auto begin = std::next(std::begin(last_used_cycles), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  return std::distance(begin, std::min_element(begin, end));
}

long qbs_lru::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                          champsim::address full_addr, access_type type)
{
  long victim = lru_way(set);
  if (intern_->upper_caches.empty())
    return victim; // nothing to query (e.g. a first-level cache)

  ++victim_selections;
  for (long q = 0; q < MAX_QUERIES; ++q) {
    ++queries;
    if (!current_set[victim].valid || !intern_->upper_holds_block(current_set[victim].address))
      return victim;

    // Resident above: treat the query as a hit, promote the candidate to MRU, and try the next one
    ++spared;
    last_used_cycles.at(static_cast<std::size_t>(set * NUM_WAY + victim)) = cycle++;
    victim = lru_way(set);
  }

  ++query_limit_hit;
  return victim;
}

void qbs_lru::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                     champsim::address victim_addr, access_type type)
{
  if (way < NUM_WAY)
    last_used_cycles.at(static_cast<std::size_t>(set * NUM_WAY + way)) = cycle++;
}

void qbs_lru::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                       champsim::address victim_addr, access_type type, uint8_t hit)
{
  // Same as LRU: promote on hits, but not on writeback hits
  if (hit && type != access_type::WRITE)
    last_used_cycles.at(static_cast<std::size_t>(set * NUM_WAY + way)) = cycle++;
}

void qbs_lru::replacement_final_stats()
{
  fmt::print("{} QBS-LRU SELECTIONS: {} QUERIES: {} SPARED: {} QUERY LIMIT HIT: {}\n", intern_->NAME, victim_selections, queries, spared, query_limit_hit);
}
