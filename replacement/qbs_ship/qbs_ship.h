#ifndef REPLACEMENT_QBS_SHIP_H
#define REPLACEMENT_QBS_SHIP_H

/*
 * Query Based Selection (QBS) on top of SHiP.
 *
 * A. Jaleel, E. Borch, M. Bhandaru, S. C. Steely Jr., J. Emer, "Achieving Non-Inclusive Cache Performance with
 * Inclusive Caches: Temporal Locality Aware (TLA) Cache Management Policies", MICRO 2010.
 * C.-J. Wu, A. Jaleel, W. Hasenplaugh, M. Martonosi, S. C. Steely Jr., J. Emer, "SHiP: Signature-based Hit
 * Predictor for High Performance Caching", MICRO 2011.
 *
 * SHiP selects victims the same way as SRRIP (a line with the maximum RRPV). Before evicting that candidate, the
 * cache queries the caches above it. If the candidate is still resident there, it is treated as a hit for
 * replacement purposes (RRPV set to 0) and the next candidate is queried. The search stops at the first non-resident
 * candidate, or after MAX_QUERIES queries, in which case the current candidate is evicted.
 *
 * Only victim selection changes. SHiP's insertion prediction and its sampler-based training are identical to
 * replacement/ship: the predictor still learns only from accesses the LLC itself observes, and a spared candidate does
 * not train it. The state is duplicated rather than shared so this module can be linked alongside ship.
 */

#include <array>
#include <cstdint>
#include <vector>

#include "cache.h"
#include "modules.h"
#include "msl/bits.h"
#include "msl/stat_methods.h"

struct qbs_ship : public champsim::modules::replacement {
private:
  int& get_rrpv(long set, long way);
  long ship_victim(long set); // ages the set until a line reaches maxRRPV, and returns it

public:
  static constexpr int maxRRPV = 3;
  static constexpr std::size_t SHCT_SIZE = 16384;
  static constexpr unsigned SHCT_PRIME = 16381;
  static constexpr unsigned SHCT_MAX = 7;

  // sampler structure
  class SAMPLER_class
  {
  public:
    bool valid = false;
    bool used = false;
    champsim::address address{};
    champsim::address ip{};
    uint64_t last_used = 0;
  };

  long NUM_SET, NUM_WAY;
  long MAX_QUERIES;
  uint64_t access_count = 0;

  // sampler
  std::vector<SAMPLER_class> sampler;
  std::vector<int> rrpv_values;
  champsim::msl::categorizer<long> set_categorizer;
  champsim::data::bits sampler_tag_bits;

  // prediction table structure
  std::vector<std::array<champsim::msl::fwcounter<champsim::msl::lg2(SHCT_MAX + 1)>, SHCT_SIZE>> SHCT;

  // QBS statistics
  uint64_t victim_selections = 0;
  uint64_t queries = 0;
  uint64_t spared = 0;
  uint64_t query_limit_hit = 0;

  explicit qbs_ship(CACHE* cache);

  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  void replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                              access_type type);
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                                access_type type, uint8_t hit);
  void replacement_final_stats();
};

#endif
