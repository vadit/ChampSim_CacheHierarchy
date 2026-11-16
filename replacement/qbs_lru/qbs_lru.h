#ifndef REPLACEMENT_QBS_LRU_H
#define REPLACEMENT_QBS_LRU_H

/*
 * Query Based Selection (QBS) on top of LRU.
 *
 * A. Jaleel, E. Borch, M. Bhandaru, S. C. Steely Jr., J. Emer, "Achieving Non-Inclusive Cache Performance with
 * Inclusive Caches: Temporal Locality Aware (TLA) Cache Management Policies", MICRO 2010.
 *
 * Before evicting the LRU candidate, the cache queries the caches above it. If the candidate is still resident
 * there, evicting it would cause an inclusion victim, so the candidate is promoted to MRU (as if it had hit) and the
 * next candidate is queried. The search stops at the first non-resident candidate, or after MAX_QUERIES queries,
 * in which case the current LRU candidate is evicted.
 *
 * QBS is designed for inclusive caches. In a non-inclusive cache it still avoids evicting blocks that are resident
 * above, but no inclusion victims are being prevented.
 */

#include <cstdint>
#include <vector>

#include "cache.h"
#include "modules.h"

class qbs_lru : public champsim::modules::replacement
{
  long NUM_WAY;
  long MAX_QUERIES;
  std::vector<uint64_t> last_used_cycles;
  uint64_t cycle = 0;

  // statistics
  uint64_t victim_selections = 0; // calls that issued at least one query
  uint64_t queries = 0;           // residency queries sent to the upper levels
  uint64_t spared = 0;            // candidates kept because they were resident above
  uint64_t query_limit_hit = 0;   // selections that ran out of queries and evicted a resident block

  long lru_way(long set) const;

public:
  explicit qbs_lru(CACHE* cache);

  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  void replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                              access_type type);
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                champsim::address victim_addr, access_type type, uint8_t hit);
  void replacement_final_stats();
};

#endif
