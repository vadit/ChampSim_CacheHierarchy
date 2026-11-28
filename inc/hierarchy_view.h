#ifndef HIERARCHY_VIEW_H
#define HIERARCHY_VIEW_H

/*
 * A read-only view of the caches above a cache, for replacement policies that use cross-level signals.
 *
 * This is the only way a hierarchy-aware policy is meant to observe the upper levels. It exposes two signals per block:
 *   - upper_resident(addr):     some cache above holds a valid copy of the block
 *   - upper_rereferenced(addr): some cache above holds a copy that has had a demand hit since it was filled there
 *
 * Both queries look through every level above (for the LLC: the L2C, then the L1D and L1I). A cache with no caches
 * above it (an L1) answers false. The view never modifies any cache.
 *
 * Hardware note: upper_resident is the residency query used by QBS. upper_rereferenced additionally needs one reuse bit
 * per upper-level block, returned with the query response.
 */

#include "address.h"
#include "cache.h"

namespace champsim
{
class hierarchy_view
{
  const CACHE* cache;

public:
  explicit hierarchy_view(const CACHE* cache_) : cache(cache_) {}

  [[nodiscard]] bool upper_resident(champsim::address addr) const { return cache->upper_holds_block(addr); }
  [[nodiscard]] bool upper_rereferenced(champsim::address addr) const { return cache->upper_block_rereferenced(addr); }
  [[nodiscard]] bool has_upper_levels() const { return !cache->upper_caches.empty(); }
};
} // namespace champsim

#endif
