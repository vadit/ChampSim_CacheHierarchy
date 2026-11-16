#ifndef REPLACEMENT_QBS_SRRIP_H
#define REPLACEMENT_QBS_SRRIP_H

/*
 * Query Based Selection (QBS) on top of SRRIP.
 *
 * A. Jaleel, E. Borch, M. Bhandaru, S. C. Steely Jr., J. Emer, "Achieving Non-Inclusive Cache Performance with
 * Inclusive Caches: Temporal Locality Aware (TLA) Cache Management Policies", MICRO 2010.
 *
 * Before evicting the SRRIP candidate (a line with the maximum RRPV), the cache queries the caches above it. If the
 * candidate is still resident there, it is treated as a hit (RRPV set to 0) and the next candidate is queried. The
 * search stops at the first non-resident candidate, or after MAX_QUERIES queries, in which case the current
 * candidate is evicted.
 *
 * The SRRIP behavior matches replacement/srrip (2-bit RRPV, insertion at maxRRPV - 1, hit promotion to 0).
 * The state is duplicated rather than shared so this module can be linked alongside srrip.
 */

#include <cstdint>
#include <vector>

#include "cache.h"
#include "modules.h"

class qbs_srrip : public champsim::modules::replacement
{
  static constexpr int maxRRPV = 3;

  long NUM_WAY;
  long MAX_QUERIES;
  std::vector<int> rrpv;

  // statistics
  uint64_t victim_selections = 0;
  uint64_t queries = 0;
  uint64_t spared = 0;
  uint64_t query_limit_hit = 0;

  int& get_rrpv(long set, long way);
  long srrip_victim(long set); // ages the set until a line reaches maxRRPV, and returns it

public:
  explicit qbs_srrip(CACHE* cache);

  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                champsim::address victim_addr, access_type type, uint8_t hit);
  void replacement_final_stats();
};

#endif
