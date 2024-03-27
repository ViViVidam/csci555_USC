/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_PERF_EVENT_HPP
#define THANOS_PERF_EVENT_HPP

#include <algorithm> // for min, max
#include <array>     // for array
#include <cstddef>   // for size_t
#include <vector>    // for vector

#include "perf_util.hpp"       // for perf_event_desc_t
#include "samples/samples.hpp" // for NUM_GROUPS, pebs (ptr only)
#include "utils/types.hpp"     // for real_t

namespace samples {
	static constexpr int MAX_BROADWELL_CTRS = 4;

	static constexpr int MIN_FREQUENCY = 1;             // Maximum accepted frequency (Hz)
	static constexpr int MAX_FREQUENCY = 1000;          // Maximum accepted frequency (Hz)
	static constexpr int DEFAULT_FREQ  = MAX_FREQUENCY; // Default frequency (Hz)

	static constexpr real_t MULTIPLIER      = 1.1; // If samples < min_[mem,ins]_samples -> then freqs *= multiplier;
	static constexpr int    MIN_MEM_SAMPLES = 300; // Minimum number of samples to reduce minimum latency
	static constexpr int    MIN_REQ_SAMPLES = 300; // Minimum number of samples to reduce requests sample periods
	static constexpr int    MIN_INS_SAMPLES = 300; // Minimum number of samples to reduce periods

	static constexpr int MMAP_PAGES = 8; // Number of pages to mmap (should be of form 2^n)

	extern int                         minimum_latency; // Minimum latency of memory samples (in ms)
	extern int                         mem_frequency;   // Frequency to be used for memory samples.
	extern int                         ins_frequency;   // Frequency to be used for instructions samples.
	extern std::array<int, NUM_GROUPS> freqs;           // Periods of sampling (1000 Hz by default)

	inline void reduce_min_latency(const real_t factor = MULTIPLIER) {
		minimum_latency = std::max<int>(1, minimum_latency * factor);
	}

	inline void increase_freqs(const real_t factor = MULTIPLIER) {
		for (int & freq : freqs) {
			freq = std::min<int>(MAX_FREQUENCY, freq * factor);
		}
	}

	inline void increase_freq_mem(const real_t factor = MULTIPLIER) {
		freqs[0] = std::min<int>(MAX_FREQUENCY, freqs[0] * factor);
	}

	inline void increase_freq_ins(const real_t factor = MULTIPLIER) {
		freqs[1] = std::min<int>(MAX_FREQUENCY, freqs[1] * factor);
	}

	auto disable_counters(int begin = 0, int end = NUM_GROUPS) -> bool;

	auto enable_counters(int begin = 0, int to_enable = NUM_GROUPS) -> bool;

	auto rotate_enabled_counters() -> bool;

	auto
	init() -> bool;

	auto read_samples() -> std::vector<samples::pebs>;

	void end();

	void update_freqs();

} // namespace samples

#endif /* end of include guard: THANOS_PERF_EVENT_HPP */
