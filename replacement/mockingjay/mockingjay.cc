#include "mockingjay.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fmt/core.h>

#include "champsim.h"

namespace
{
int clamp_bits(int value, int lo, int hi) { return std::clamp(value, lo, hi); }
} // namespace

mockingjay::mockingjay(CACHE* cache)
    : replacement(cache), NUM_SET(cache->NUM_SET), NUM_WAY(cache->NUM_WAY), LOG2_BLOCK_SIZE(static_cast<int>(champsim::to_underlying(cache->OFFSET_BITS))),
      LOG2_SET(static_cast<int>(champsim::lg2(static_cast<uint64_t>(NUM_SET))))
{
  // The original computed LOG2_LLC_SIZE = LOG2_LLC_SET + log2(LLC_WAY) + LOG2_BLOCK_SIZE, truncated to int
  LOG2_SIZE = static_cast<int>(LOG2_SET + std::log2(static_cast<double>(NUM_WAY)) + LOG2_BLOCK_SIZE);

  // Guard the derived widths so small caches (or unusual geometries) still produce valid shifts
  LOG2_SAMPLED_SETS = clamp_bits(LOG2_SIZE - 16, 0, LOG2_SET);
  SAMPLED_CACHE_TAG_BITS = clamp_bits(31 - LOG2_SIZE, 1, 63);
  PC_SIGNATURE_BITS = clamp_bits(LOG2_SIZE - 10, 1, 63);

  INF_RD = static_cast<int>(NUM_WAY) * HISTORY - 1;
  INF_ETR = (static_cast<int>(NUM_WAY) * HISTORY / GRANULARITY) - 1;
  MAX_RD = INF_RD - 22;

  FLEXMIN_PENALTY = 2.0 - std::log2(static_cast<double>(NUM_CPUS)) / 4.0;

  // The sampler timestamps must be able to represent INF_RD
  assert(INF_RD < (1 << TIMESTAMP_BITS));

  etr.assign(static_cast<std::size_t>(NUM_SET * NUM_WAY), 0);
  etr_clock.assign(static_cast<std::size_t>(NUM_SET), GRANULARITY);
  current_timestamp.assign(static_cast<std::size_t>(NUM_SET), 0);

  for (long set = 0; set < NUM_SET; ++set) {
    if (is_sampled_set(set)) {
      for (uint64_t i = 0; i < (uint64_t{1} << LOG2_SAMPLED_CACHE_SETS); ++i) {
        sampled_cache.emplace(static_cast<uint64_t>(set) + (i << LOG2_SET), std::vector<sampled_cache_line>(SAMPLED_CACHE_WAYS));
      }
    }
  }
}

int& mockingjay::get_etr(long set, long way) { return etr.at(static_cast<std::size_t>(set * NUM_WAY + way)); }

bool mockingjay::is_sampled_set(long set) const
{
  int mask_length = LOG2_SET - LOG2_SAMPLED_SETS;
  long mask = (1L << mask_length) - 1;
  return (set & mask) == ((set >> (LOG2_SET - mask_length)) & mask);
}

uint64_t mockingjay::get_pc_signature(uint64_t pc, bool hit, bool prefetch, uint32_t core) const
{
  auto crc_hash = [](uint64_t block_address) {
    constexpr unsigned long long crc_polynomial = 3988292384ULL;
    unsigned long long return_val = block_address;
    for (unsigned int i = 0; i < 3; i++)
      return_val = ((return_val & 1) == 1) ? ((return_val >> 1) ^ crc_polynomial) : (return_val >> 1);
    return return_val;
  };

  if (NUM_CPUS == 1) {
    pc = pc << 1;
    if (hit)
      pc = pc | 1;
    pc = pc << 1;
    if (prefetch)
      pc = pc | 1;
  } else {
    pc = pc << 1;
    if (prefetch)
      pc = pc | 1;
    pc = pc << 2;
    pc = pc | core;
  }
  pc = crc_hash(pc);
  pc = (pc << (64 - PC_SIGNATURE_BITS)) >> (64 - PC_SIGNATURE_BITS);
  return pc;
}

uint64_t mockingjay::get_sampled_cache_index(long set, uint64_t full_addr) const
{
  // The original took the low (LOG2_SAMPLED_CACHE_SETS + LOG2_LLC_SET) bits of the block address.
  // The low LOG2_SET of those bits are the set index, so build the index from the set directly.
  uint64_t upper = (full_addr >> (LOG2_BLOCK_SIZE + LOG2_SET)) & ((uint64_t{1} << LOG2_SAMPLED_CACHE_SETS) - 1);
  return static_cast<uint64_t>(set) | (upper << LOG2_SET);
}

uint64_t mockingjay::get_sampled_cache_tag(uint64_t x) const
{
  x >>= LOG2_SET + LOG2_BLOCK_SIZE + LOG2_SAMPLED_CACHE_SETS;
  x = (x << (64 - SAMPLED_CACHE_TAG_BITS)) >> (64 - SAMPLED_CACHE_TAG_BITS);
  return x;
}

int mockingjay::search_sampled_cache(uint64_t tag, std::vector<sampled_cache_line>& sampled_set) const
{
  for (int way = 0; way < SAMPLED_CACHE_WAYS; way++) {
    if (sampled_set[static_cast<std::size_t>(way)].valid && (sampled_set[static_cast<std::size_t>(way)].tag == tag))
      return way;
  }
  return -1;
}

void mockingjay::detrain(sampled_cache_line& line)
{
  if (!line.valid)
    return;

  if (auto it = rdp.find(line.signature); it != rdp.end())
    it->second = std::min(it->second + 1, INF_RD);
  else
    rdp[line.signature] = INF_RD;

  line.valid = false;
}

int mockingjay::temporal_difference(int init, int sample) const
{
  // Preserved exactly from the original, including the integer truncation of diff * TEMP_DIFFERENCE
  if (sample > init) {
    int diff = sample - init;
    diff = static_cast<int>(diff * TEMP_DIFFERENCE);
    diff = std::min(1, diff);
    return std::min(init + diff, INF_RD);
  }
  if (sample < init) {
    int diff = init - sample;
    diff = static_cast<int>(diff * TEMP_DIFFERENCE);
    diff = std::min(1, diff);
    return std::max(init - diff, 0);
  }
  return init;
}

int mockingjay::increment_timestamp(int input)
{
  input++;
  input = input % (1 << TIMESTAMP_BITS);
  return input;
}

int mockingjay::time_elapsed(int global, int local)
{
  if (global >= local)
    return global - local;
  global = global + (1 << TIMESTAMP_BITS);
  return global - local;
}

long mockingjay::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                             champsim::address full_addr, access_type type)
{
  // If there's an invalid block, we don't need to evict any valid ones
  for (long way = 0; way < NUM_WAY; way++) {
    if (!current_set[way].valid)
      return way;
  }

  // Evict the line with the largest |ETR|, preferring lines that are already overdue (negative ETR)
  int max_etr = 0;
  long victim_way = 0;
  for (long way = 0; way < NUM_WAY; way++) {
    int value = get_etr(set, way);
    if (std::abs(value) > max_etr || (std::abs(value) == max_etr && value < 0)) {
      max_etr = std::abs(value);
      victim_way = way;
    }
  }

  // Bypass if the incoming line is predicted to be reused later than every resident line
  uint64_t pc_signature = get_pc_signature(ip.to<uint64_t>(), false, type == access_type::PREFETCH, triggering_cpu);
  if (type != access_type::WRITE) {
    if (auto it = rdp.find(pc_signature); it != rdp.end() && (it->second > MAX_RD || it->second / GRANULARITY > max_etr)) {
      if (intern_->bypass_allowed()) {
        ++bypasses;
        return NUM_WAY;
      }
      ++bypasses_suppressed;
    }
  }

  return victim_way;
}

void mockingjay::update(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, access_type type, bool hit)
{
  if (type == access_type::WRITE) {
    if (!hit && way < NUM_WAY)
      get_etr(set, way) = -INF_ETR;
    return;
  }

  uint64_t pc = get_pc_signature(ip.to<uint64_t>(), hit, type == access_type::PREFETCH, triggering_cpu);
  auto set_idx = static_cast<std::size_t>(set);

  if (is_sampled_set(set)) {
    uint64_t raw_addr = full_addr.to<uint64_t>();
    auto sampled_entry = sampled_cache.find(get_sampled_cache_index(set, raw_addr));
    assert(sampled_entry != sampled_cache.end());
    auto& sampled_set = sampled_entry->second;
    uint64_t sampled_cache_tag = get_sampled_cache_tag(raw_addr);
    int sampled_cache_way = search_sampled_cache(sampled_cache_tag, sampled_set);

    // Train the predictor on an observed reuse
    if (sampled_cache_way > -1) {
      auto& line = sampled_set[static_cast<std::size_t>(sampled_cache_way)];
      uint64_t last_signature = line.signature;
      int sample = time_elapsed(current_timestamp[set_idx], line.timestamp);

      if (sample <= INF_RD) {
        if (type == access_type::PREFETCH)
          sample = static_cast<int>(sample * FLEXMIN_PENALTY);

        if (auto it = rdp.find(last_signature); it != rdp.end())
          it->second = temporal_difference(it->second, sample);
        else
          rdp[last_signature] = sample;

        line.valid = false;
      }
    }

    // Find the sampler line to replace, detraining any lines that have aged past INF_RD
    int lru_way = -1;
    int lru_rd = -1;
    for (int w = 0; w < SAMPLED_CACHE_WAYS; w++) {
      auto& line = sampled_set[static_cast<std::size_t>(w)];
      if (!line.valid) {
        lru_way = w;
        lru_rd = INF_RD + 1;
        continue;
      }

      int sample = time_elapsed(current_timestamp[set_idx], line.timestamp);
      if (sample > INF_RD) {
        lru_way = w;
        lru_rd = INF_RD + 1;
        detrain(line);
      } else if (sample > lru_rd) {
        lru_way = w;
        lru_rd = sample;
      }
    }
    assert(lru_way >= 0);
    detrain(sampled_set[static_cast<std::size_t>(lru_way)]);

    for (auto& line : sampled_set) {
      if (!line.valid) {
        line.valid = true;
        line.signature = pc;
        line.tag = sampled_cache_tag;
        line.timestamp = current_timestamp[set_idx];
        break;
      }
    }

    current_timestamp[set_idx] = increment_timestamp(current_timestamp[set_idx]);
  }

  // Age every other line in the set once every GRANULARITY accesses
  if (etr_clock[set_idx] == GRANULARITY) {
    for (long w = 0; w < NUM_WAY; w++) {
      if (w != way && std::abs(get_etr(set, w)) < INF_ETR)
        get_etr(set, w)--;
    }
    etr_clock[set_idx] = 0;
  }
  etr_clock[set_idx]++;

  // Set the ETR of the accessed line from the predictor
  if (way < NUM_WAY) {
    if (auto it = rdp.find(pc); it == rdp.end()) {
      get_etr(set, way) = (NUM_CPUS == 1) ? 0 : INF_ETR;
    } else if (it->second > MAX_RD) {
      get_etr(set, way) = INF_ETR;
    } else {
      get_etr(set, way) = it->second / GRANULARITY;
    }
  }
}

void mockingjay::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                        champsim::address victim_addr, access_type type)
{
  update(triggering_cpu, set, way, full_addr, ip, type, false);
}

void mockingjay::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                          champsim::address victim_addr, access_type type, uint8_t hit)
{
  // The original policy is only updated on hits here; misses are handled when the fill arrives
  if (hit)
    update(triggering_cpu, set, way, full_addr, ip, type, true);
}

void mockingjay::replacement_final_stats()
{
  fmt::print("{} Mockingjay BYPASSES: {} SUPPRESSED (inclusive): {} RDP ENTRIES: {}\n", intern_->NAME, bypasses, bypasses_suppressed, rdp.size());
}
