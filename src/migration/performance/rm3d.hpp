/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_RM3D_HPP
#define THANOS_RM3D_HPP

#include <algorithm>          // for fill, max_element
#include <chrono>             // for system_clock::time_...
#include <cmath>              // for isnormal, pow, exp
#include <ext/alloc_traits.h> // for __alloc_traits<>::v...
#include <iostream>           // for ostream
#include <numeric>            // for accumulate
#include <string>             // for string, to_string
#include <unistd.h>           // for sysconf, _SC_LEVEL1...
#include <variant>            // for variant
#include <vector>             // for vector, vector<>::c...

#include "migration/performance/performance.hpp" // for PERFORMANCE_INVALID...
#include "migration/utils/inst_sample.hpp"       // for inst_sample_t
#include "migration/utils/mem_sample.hpp"        // for memory_sample_t
#include "migration/utils/reqs_sample.hpp"       // for reqs_sample_t
#include "samples/perf_event/perf_event.hpp"     // for minimum_latency
#include "system_info/system_info.hpp"           // for num_of_nodes, node_...
#include "tabulate/tabulate.hpp"                 // for Table, Format, Font...
#include "utils/string.hpp"                      // for to_string
#include "utils/time.hpp"                        // for time_until_now
#include "utils/types.hpp"                       // for real_t, req_t, time...

namespace performance {
	static const size_t CACHE_LINE_SIZE = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);

	// Based on Óscar's work in his PhD. This is associated for a single TID
	class rm3d {
	private:
		static constexpr real_t ALPHA = 1.0;
		static constexpr real_t BETA  = 1.0;
		static constexpr real_t GAMMA = 1.0;

		std::vector<ins_t> flops_{};      // Number of Floating Point operations executed in each node.
		std::vector<ins_t> inst_{};       // Number of instructions executed in each node.
		std::vector<req_t> total_reqs_{}; // Total number of memory requests in each node (from "REQ" samples).
		std::vector<tim_t> times_{};      // Time (in nanoseconds) consumed by the instructions and requests.

		std::vector<req_t> node_reqs_{}; // Number of memory requests to each memory node (from memory samples).
		std::vector<lat_t> mean_lat_{};  // Mean latency of memory accesses to each node. Integer is precise enough.

		std::vector<real_t>     perfs_{};        // 3DyRM performance per memory node.
		std::vector<time_point> perfs_time_{};   // Time at which 3DyRM performance was updated by last time.
		std::vector<bool>       perfs_update_{}; // 3DyRM performance needs to be recalculated.

	public:
		rm3d() :
		    flops_(system_info::max_node() + 1, 0),
		    inst_(system_info::max_node() + 1, 0),
		    total_reqs_(system_info::max_node() + 1, 0),
		    times_(system_info::max_node() + 1, 0),
		    node_reqs_(system_info::max_node() + 1, 0),
		    mean_lat_(system_info::max_node() + 1, samples::minimum_latency),
		    perfs_(system_info::max_node() + 1, PERFORMANCE_INVALID_VALUE),
		    perfs_time_(system_info::max_node() + 1, hres_clock::now()),
		    perfs_update_(system_info::max_node() + 1, false) {
		}

		inline void add_data(const inst_sample_t & data) {
			const auto node = system_info::node_from_cpu(data.cpu());

			if (data.flop()) {
				flops_[node] += data.inst() * data.multiplier();
			} else {
				inst_[node] += data.inst() * data.multiplier();
			}
			times_[node] += data.time();

			perfs_update_[node] = true;
		}

		inline void add_data(const reqs_sample_t & data) {
			const auto node = system_info::node_from_cpu(data.cpu());

			total_reqs_[node] += data.reqs();

			perfs_update_[node] = true;
		}

		inline void add_data(const memory_sample_t & data) {
			const auto src_node = system_info::node_from_cpu(data.cpu());
			const auto dst_node = data.page_node();

			const auto latency = data.latency();
			const auto reqs    = data.reqs();

			mean_lat_[dst_node] =
			    (mean_lat_[dst_node] * node_reqs_[dst_node] + latency * reqs) / (node_reqs_[dst_node] + reqs);
			node_reqs_[dst_node] += reqs;

			perfs_update_[src_node] = true;
		}

		[[nodiscard]] inline auto ops_per_second(const node_t node) const {
			if (!std::isnormal(times_[node])) { return real_t(); }

			const real_t seconds =
			    static_cast<real_t>(times_[node]) / 1e9F; // 10^9 as times are measured in nanoseconds

			return static_cast<real_t>(inst_[node] + flops_[node]) / seconds;
		}

		[[nodiscard]] inline auto ops_per_byte(const node_t node) const {
			if (!std::isnormal(total_reqs_[node])) { return real_t(); }

			return ops_per_second(node) / static_cast<real_t>(total_reqs_[node] * CACHE_LINE_SIZE);
		}

		[[nodiscard]] inline auto av_latency(const node_t node) const {
			// If the average latency is negative, take minimum_latency.
			// Sanity check since negative latency is physically impossible.
			return (!std::isnormal(total_reqs_[node]) || mean_lat_[node] <= 0) ? samples::minimum_latency :
			                                                                     mean_lat_[node];
		}

		static inline auto calc_perf(const real_t ops_per_s, const real_t ops_per_b, const lat_t mean_lat) {
			real_t result = 0.0;

			// Compute first the division in order to reduce chances of overflow
			// as ops_per_s is expected to be huge -> ops_per_s * ops_per_b to be potentially HUGE
			result = std::pow(ops_per_s, BETA) *
			         (std::pow(ops_per_b, GAMMA) / std::pow(static_cast<real_t>(mean_lat), ALPHA));

			return std::isnormal(result) ? result : PERFORMANCE_INVALID_VALUE;
		}

		inline void calc_perf(const node_t node) {
			if (!std::isnormal(times_[node]) || !std::isnormal(total_reqs_[node])) { // No data
				perfs_[node] = PERFORMANCE_INVALID_VALUE;
				return;
			}

			const auto ops_per_s = ops_per_second(node);
			const auto ops_per_b = ops_per_byte(node);
			const auto mean_lat  = av_latency(node);

			perfs_update_[node] = false;
			perfs_time_[node]   = hres_clock::now();

			perfs_[node] = calc_perf(ops_per_s, ops_per_b, mean_lat);
		}

		inline void calc_perf() {
			for (const auto & node : system_info::nodes()) {
				if (perfs_update_[node]) { calc_perf(node); }
			}
		}

		// Function to compute the decay. According to temporal locality principle, recently accessed data is likely to
		// be accessed again (that's why perf is stored for each node). Though, the chances of accessing the same data
		// (supposed to be in the same node) decays rapidly with time.
		// Returns the decay factor (to be multiplied). 1 = no decay. 0 = absolute decay when time tends to infinity.
		[[nodiscard]] inline auto decay(const node_t node) const -> real_t {
			static constexpr real_t t_min = 1;  // Seconds until decay starts working
			static constexpr real_t p     = 3;  // Power of the exponential (how fast it decays)
			static constexpr real_t d     = 30; // Denominator (displaces decay left-right)

			const auto t = utils::time::time_until_now(perfs_time_[node]);

			return t < t_min ? real_t(1) : std::exp(-std::pow(t, p) / d);
		}

		[[nodiscard]] inline auto perf_node(const node_t node) const -> real_t {
			const auto perf = perfs_[node];
			return (perf < 0 || !std::isnormal(perf)) ? PERFORMANCE_INVALID_VALUE : perf * decay(node);
		}

		[[nodiscard]] inline auto raw_perf_node(const node_t node) const -> real_t {
			const auto perf = perfs_[node];
			return (perf < 0 || !std::isnormal(perf)) ? PERFORMANCE_INVALID_VALUE : perf;
		}

		[[nodiscard]] inline auto preferred_node() const -> node_t {
			return static_cast<node_t>(std::max_element(node_reqs_.begin(), node_reqs_.end()) - node_reqs_.begin());
		}

		[[nodiscard]] inline auto node_reqs(const node_t node) const -> req_t {
			return node_reqs_[node];
		}

		[[nodiscard]] inline auto scaled_node_reqs() const {
			const auto scale = std::accumulate(total_reqs_.begin(), total_reqs_.end(), 0.0) /
			                   std::accumulate(node_reqs_.begin(), node_reqs_.end(), 0.0);

			if (!std::isnormal(scale) || scale <= 0) { return node_reqs_; }

			auto scaled_reqs = node_reqs_;

			for (auto & reqs : scaled_reqs) {
				reqs = static_cast<req_t>(static_cast<real_t>(reqs) * scale);
			}

			return scaled_reqs;
		}

		[[nodiscard]] inline auto scaled_node_reqs(const node_t node) const {
			const auto scale =
			    static_cast<real_t>(total_reqs_[node]) / (std::accumulate(node_reqs_.begin(), node_reqs_.end(), 0.0));

			auto scaled_reqs = node_reqs_;

			for (auto & reqs : scaled_reqs) {
				reqs *= static_cast<req_t>(static_cast<real_t>(reqs) * scale);
			}

			return scaled_reqs;
		}

		inline void reset() {
			for (const auto & node : system_info::nodes()) {
				flops_[node]      = {};
				inst_[node]       = {};
				total_reqs_[node] = {};
				times_[node]      = {};

				node_reqs_[node] = {};
				mean_lat_[node]  = {};
			}
		}

		inline void hard_reset() {
			reset();
			fill(perfs_.begin(), perfs_.end(), PERFORMANCE_INVALID_VALUE);
			fill(perfs_update_.begin(), perfs_update_.end(), false);
			fill(perfs_time_.begin(), perfs_time_.end(), hres_clock::now());
		}

		[[nodiscard]] inline auto operator[](const node_t node) const {
			return perf_node(node);
		}

		friend auto operator<<(std::ostream & os, const rm3d & d) -> std::ostream & {
			tabulate::Table table;

			table.add_row({ "NODE", "FLOPS", "INSTS", "TOTAL_REQS", "TIMES", "AV. LATENCY", "PERF WO/DECAY", "DECAY",
			                "PERF W/DECAY" });

			for (const auto & node : system_info::nodes()) {
				table.add_row(
				    { std::to_string(node), utils::string::to_string(d.flops_[node], 0),
				      utils::string::to_string(d.inst_[node], 0), utils::string::to_string(d.total_reqs_[node], 0),
				      utils::string::to_string(d.times_[node], 0), utils::string::to_string(d.av_latency(node), 2),
				      utils::string::to_string(d.perfs_[node], 0), utils::string::to_string(d.decay(node), 3),
				      utils::string::to_string(d.perf_node(node), 0) });
			}

			table.format().hide_border().font_align(tabulate::FontAlign::right);
			table.print(os);

			return os;
		}
	};
} // namespace performance

#endif /* end of include guard: THANOS_RM3D_HPP */
