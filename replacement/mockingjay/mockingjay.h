#ifndef REPLACEMENT_MOCKINGJAY_H
#define REPLACEMENT_MOCKINGJAY_H

/*
 * Mockingjay replacement policy
 *
 * I. Shah, A. Jain, C. Lin, "Effective Mimicry of Belady's MIN Policy", HPCA 2022.
 *
 * Ported from the original CRC2-era ChampSim implementation (global LLC_SET/LLC_WAY arrays,
 * llc_find_victim/llc_update_replacement_state hooks) to the modular replacement interface.
 * All geometry-dependent constants are now derived from the cache the module is attached to,
 * so the policy can be instantiated on any cache, though it is designed and tuned for the LLC.
 *
 * Behavioural notes on the port:
 *  - The old update hook was called on hits and on fills (including bypassed fills, with way == LLC_WAY).
 *    Here, update_replacement_state() handles hits (misses are ignored) and replacement_cache_fill() handles fills.
 *  - WRITEBACK in the old interface corresponds to access_type::WRITE at the levels below L1.
 *  - Bypassing is suppressed when the cache is configured as inclusive (CACHE::bypass_allowed()).
 */

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cache.h"
#include "modules.h"

class mockingjay : public champsim::modules::replacement
{
public:
  // Tunables from the original implementation
  static constexpr int HISTORY = 8;
  static constexpr int GRANULARITY = 8;
  static constexpr int SAMPLED_CACHE_WAYS = 5;
  static constexpr int LOG2_SAMPLED_CACHE_SETS = 4;
  static constexpr int TIMESTAMP_BITS = 8;
  static constexpr double TEMP_DIFFERENCE = 1.0 / 16.0;

private:
  struct sampled_cache_line {
    bool valid = false;
    uint64_t tag = 0;
    uint64_t signature = 0;
    int timestamp = 0;
  };

  long NUM_SET, NUM_WAY;
  int LOG2_BLOCK_SIZE;
  int LOG2_SET;
  int LOG2_SIZE;
  int LOG2_SAMPLED_SETS;
  int INF_RD;
  int INF_ETR;
  int MAX_RD;
  int SAMPLED_CACHE_TAG_BITS;
  int PC_SIGNATURE_BITS;
  double FLEXMIN_PENALTY;

  std::vector<int> etr;               // estimated time remaining, [set * NUM_WAY + way]
  std::vector<int> etr_clock;         // per-set aging clock
  std::vector<int> current_timestamp; // per-set sampler timestamp
  std::unordered_map<uint64_t, int> rdp; // reuse distance predictor, keyed by PC signature
  std::unordered_map<uint64_t, std::vector<sampled_cache_line>> sampled_cache;

  // statistics
  uint64_t bypasses = 0;
  uint64_t bypasses_suppressed = 0;

  int& get_etr(long set, long way);

  [[nodiscard]] bool is_sampled_set(long set) const;
  [[nodiscard]] uint64_t get_pc_signature(uint64_t pc, bool hit, bool prefetch, uint32_t core) const;
  [[nodiscard]] uint64_t get_sampled_cache_index(long set, uint64_t full_addr) const;
  [[nodiscard]] uint64_t get_sampled_cache_tag(uint64_t full_addr) const;
  int search_sampled_cache(uint64_t tag, std::vector<sampled_cache_line>& sampled_set) const;
  void detrain(sampled_cache_line& line);
  [[nodiscard]] int temporal_difference(int init, int sample) const;
  [[nodiscard]] static int increment_timestamp(int input);
  [[nodiscard]] static int time_elapsed(int global, int local);

  void update(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, access_type type, bool hit);

public:
  explicit mockingjay(CACHE* cache);

  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  void replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                              access_type type);
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                champsim::address victim_addr, access_type type, uint8_t hit);
  void replacement_final_stats();
};

#endif
