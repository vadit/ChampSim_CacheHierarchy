#ifndef CACHE_STATS_H
#define CACHE_STATS_H

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "channel.h"
#include "event_counter.h"

struct cache_stats {
  std::string name;
  // prefetch stats
  uint64_t pf_requested = 0;
  uint64_t pf_issued = 0;
  uint64_t pf_useful = 0;
  uint64_t pf_useless = 0;
  uint64_t pf_fill = 0;

  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> hits = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> misses = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> miss_merge = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> fill = {};

  long total_miss_latency_cycles{};

  // inclusion stats
  uint64_t back_inval_issued = 0;   // upper-level blocks invalidated because this (inclusive) cache evicted them
  uint64_t back_inval_received = 0; // blocks in this cache invalidated because a lower inclusive cache evicted them
  uint64_t back_inval_dirty = 0;    // evictions from this cache that were written back because an upper copy was dirty
  uint64_t excl_promotions = 0;     // hits from an upper cache that moved the block up out of this (exclusive) cache
  uint64_t excl_fills_skipped = 0;  // fills not allocated here because an upper cache will hold the block
  uint64_t clean_victims_received = 0; // clean evictions from upper caches inserted into this (exclusive) cache
};

cache_stats operator-(cache_stats lhs, cache_stats rhs);

#endif
