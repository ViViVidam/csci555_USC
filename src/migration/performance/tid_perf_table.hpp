/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_TID_PERF_TABLE_HPP
#define THANOS_TID_PERF_TABLE_HPP

#include <algorithm>          // for fill
#include <cmath>              // for isnormal
#include <ext/alloc_traits.h> // for __alloc_traits<>::v...
#include <iostream>           // for operator<<, ostream
#include <memory>             // for allocator_traits<>:...
#include <string>             // for string, to_string
#include <sys/types.h>        // for pid_t, size_t
#include <type_traits>        // for add_const<>::type
#include <variant>            // for variant
#include <vector>             // for vector

#include "migration/performance/performance.hpp" // for PERFORMANCE_INVALID...
#include "migration/performance/rm3d.hpp"        // for rm3d, operator<<
#include "migration/utils/inst_sample.hpp"       // for inst_sample_t
#include "migration/utils/mem_sample.hpp"        // for memory_sample_t
#include "migration/utils/reqs_sample.hpp"       // for reqs_sample_t
#include "samples/perf_event/perf_event.hpp"     // for minimum_latency
#include "system_info/system_info.hpp"           // for num_of_nodes, pid_f...
#include "tabulate/tabulate.hpp"                 // for Table, Format, Font...
#include "utils/string.hpp"                      // for to_string, percentage
#include "utils/types.hpp"                       // for real_t, node_t
#include "utils/verbose.hpp"                     // for LVL4, lvl

namespace performance {
	namespace details {
		class row {
		private:
			rm3d performance_ = {};    // Performance data (Roofline Model 3D)
			bool running_     = false; // Is the thread running?

		public:
			row() = default;

			[[nodiscard]] inline auto running() const -> bool {
				return running_;
			}

			inline void running(const bool running) {
				running_ = running;
			}

			inline void clear() {
				performance_.reset();
				running_ = false;
			}

			template<class T>
			inline void add_data(const T & data) {
				performance_.add_data(data);
				running_ = true;
			}

			inline void calc_perf() {
				performance_.calc_perf();
			}

			[[nodiscard]] inline auto performance() const -> const auto & {
				return performance_;
			}

			[[nodiscard]] inline auto perf_in_node(const node_t node) const -> real_t {
				return performance_.perf_node(node);
			}

			[[nodiscard]] inline auto raw_perf_in_node(const node_t node) const -> real_t {
				return performance_.raw_perf_node(node);
			}

			[[nodiscard]] inline auto preferred_node() const -> node_t {
				return performance_.preferred_node();
			}

			[[nodiscard]] inline auto reqs_per_node() const {
				return performance_.scaled_node_reqs();
			}

			[[nodiscard]] inline auto ops_per_s(const node_t node) const {
				return performance_.ops_per_second(node);
			}

			[[nodiscard]] inline auto ops_per_byte(const node_t node) const {
				return performance_.ops_per_byte(node);
			}

			[[nodiscard]] inline auto av_latency(const node_t node) const {
				return performance_.av_latency(node);
			}

			friend auto operator<<(std::ostream & os, const row & row) -> std::ostream & {
				os << row.performance_;
				return os;
			}
		};
	} // namespace details

	class tid_perf_table {
	private:
		mutable fast_umap<pid_t, details::row> table_ = {};

		mutable fast_umap<pid_t, real_t> mean_perf_pid_    = {};
		mutable fast_umap<pid_t, real_t> mean_cpu_use_pid_ = {};

		real_t total_performance_ = 0;

		real_t mean_perf_    = 0;
		real_t mean_cpu_use_ = 0;

		req_t accesses_   = 0;
		lat_t av_latency_ = samples::minimum_latency;

		// av_latencies_[src][dst] = latency of memory operations from node src to node dst
		std::vector<std::vector<lat_t>> av_latencies_{}; // integer gives enough precision, no need for floats
		std::vector<std::vector<req_t>> mem_accesses_{};

	public:
		tid_perf_table() noexcept :
		    av_latencies_(system_info::max_node() + 1,
		                  std::vector<lat_t>(system_info::max_node() + 1, samples::minimum_latency)),
		    mem_accesses_(system_info::max_node() + 1, std::vector<req_t>(system_info::max_node() + 1, 0)) {
		}

		[[nodiscard]] inline auto begin() const {
			return table_.begin();
		}

		[[nodiscard]] inline auto end() const {
			return table_.end();
		}

		[[nodiscard]] inline auto size() const {
			return table_.size();
		}

		inline void update() {
			check_running();
			calc_perf();
		}

		inline void add_data(const memory_sample_t & sample) {
			auto & row = table_[sample.tid()];
			row.add_data(sample);

			const auto & src = system_info::node_from_cpu(sample.cpu());
			const auto & dst = sample.page_node();

			const auto latency = sample.latency();
			const auto reqs    = sample.reqs();

			av_latencies_[src][dst] =
			    (mem_accesses_[src][dst] * av_latencies_[src][dst] + latency * reqs) / (mem_accesses_[src][dst] + reqs);
			mem_accesses_[src][dst] += reqs;

			av_latency_ = (av_latency_ * accesses_ + latency * reqs) / (accesses_ + reqs);
			accesses_ += reqs;
		}

		inline void add_data(const reqs_sample_t & sample) {
			auto & row = table_[sample.tid()];
			row.add_data(sample);
		}

		inline void add_data(const inst_sample_t & sample) {
			auto & row = table_[sample.tid()];
			row.add_data(sample);
		}

		inline void add_tids(const set<pid_t> & tids) {
			for (const auto & tid : tids) {
				if (!table_.contains(tid)) {
					table_[tid] = {};

					const auto pid = system_info::pid_from_tid(tid);
					mean_perf_pid_[pid] += 0;
					mean_cpu_use_pid_[pid] += 0;
				}
			}
		}

		inline void clear_it() {
			check_alive_tids();
			for (auto & [tid, row] : table_) {
				row.clear();
			}
			for (auto & [pid, mean] : mean_perf_pid_) {
				mean = 0;
			}
			for (auto & [pid, mean] : mean_cpu_use_pid_) {
				mean = 0;
			}

			total_performance_ = 0;

			accesses_   = 0;
			av_latency_ = samples::minimum_latency;
			for (const auto & node : system_info::nodes()) {
				std::fill(av_latencies_[node].begin(), av_latencies_[node].end(), samples::minimum_latency);
				std::fill(mem_accesses_[node].begin(), mem_accesses_[node].end(), 0);
			}
		}

		inline void hard_clear() {
			table_.clear();
			mean_perf_pid_.clear();
			mean_cpu_use_pid_.clear();
			accesses_   = 0;
			av_latency_ = samples::minimum_latency;
			for (const auto & node : system_info::nodes()) {
				std::fill(av_latencies_[node].begin(), av_latencies_[node].end(), samples::minimum_latency);
				std::fill(mem_accesses_[node].begin(), mem_accesses_[node].end(), 0);
			}
		}

		inline void remove_entry(const pid_t tid) {
			table_.erase(tid);
		}

		inline void check_alive_tids() {
			std::vector<pid_t> tids_to_remove;

			for (const auto & [tid, row] : table_) {
				if (!system_info::is_pid_alive(tid)) { tids_to_remove.emplace_back(tid); }
			}

			for (const auto & tid : tids_to_remove) {
				remove_entry(tid);
			}
		}

		inline void check_running() {
			for (auto & [tid, row] : table_) {
				if (!row.running() && system_info::is_running(tid)) { row.running(true); }
			}
		}

		[[nodiscard]] inline auto is_running(const pid_t tid) const -> bool {
			return table_[tid].running();
		}

		inline void calc_perf() {
			real_t temp_mean  = 0;
			real_t temp_cpu   = 0;
			size_t valid_perf = 0;

			total_performance_ = 0;

			umap<pid_t, size_t> valid_perf_pid;

			for (auto & [tid, row] : table_) {
				if (!row.running()) { continue; }

				row.calc_perf();

				const auto node = system_info::pinned_node_from_tid(tid);
				const auto perf = row.perf_in_node(node);

				if (perf < 0) { continue; }

				const auto cpu_use = system_info::cpu_use(tid);

				total_performance_ += perf;

				temp_mean += perf;
				temp_cpu += cpu_use;

				const auto pid = system_info::pid_from_tid(tid);

				mean_perf_pid_[pid] += perf;
				mean_cpu_use_pid_[pid] += cpu_use;

				++valid_perf;
				++valid_perf_pid[pid];
			}

			mean_perf_ = temp_mean / static_cast<real_t>(valid_perf);
			mean_perf_ = std::isnormal(mean_perf_) ? mean_perf_ : real_t(1.0);

			mean_cpu_use_ = temp_cpu / static_cast<real_t>(valid_perf);
			mean_cpu_use_ = std::isnormal(mean_cpu_use_) ? mean_cpu_use_ : real_t(1.0);

			for (const auto & [pid, valid] : valid_perf_pid) {
				const auto aux_perf = mean_perf_pid_[pid] / static_cast<real_t>(valid);
				mean_perf_pid_[pid] = std::isnormal(aux_perf) ? aux_perf : real_t(1.0);

				const auto aux_cpu_use = mean_cpu_use_pid_[pid] / static_cast<real_t>(valid);
				mean_cpu_use_pid_[pid] = std::isnormal(aux_cpu_use) ? aux_cpu_use : real_t(1.0);
			}
		}

		[[nodiscard]] inline auto av_latency(const node_t src, const node_t dst) const {
			return av_latencies_[src][dst];
		}

		[[nodiscard]] inline auto av_latency() const {
			return av_latency_;
		}

		[[nodiscard]] inline auto get_rm3d(const pid_t tid) const -> const auto & {
			return table_[tid].performance();
		}

		[[nodiscard]] inline auto performance(const pid_t tid, const node_t node) const -> real_t {
			return table_[tid].perf_in_node(node);
		}

		[[nodiscard]] inline auto performance(const pid_t tid) const -> real_t {
			const auto node = system_info::pinned_node_from_tid(tid);
			return performance(tid, node);
		}

		[[nodiscard]] inline auto raw_performance(const pid_t tid, const node_t node) const {
			return table_[tid].raw_perf_in_node(node);
		}

		[[nodiscard]] inline auto raw_performance(const pid_t tid) const {
			const auto node = system_info::pinned_node_from_tid(tid);
			return raw_performance(tid, node);
		}

		[[nodiscard]] inline auto rel_performance(const pid_t tid) const {
			const auto   pid  = system_info::pid_from_tid(tid);
			const auto & perf = performance(tid);

			// Compared to threads with same PID
			const auto rel_perf = (perf == performance::PERFORMANCE_INVALID_VALUE) ?
			                          (system_info::cpu_use(tid) / mean_cpu_use_pid_[pid]) :
			                          (perf / mean_perf_pid_[pid]);

			// Compared to all other threads
			// const auto rel_perf = (perf == performance::PERFORMANCE_INVALID_VALUE) ?
			//                           (system_info::cpu_use(tid) / mean_cpu_use_) :
			//                           (perf / mean_perf_);

			return std::isnormal(rel_perf) ? rel_perf : performance::PERFORMANCE_INVALID_VALUE;
		}

		[[nodiscard]] inline auto total_performance() const -> real_t {
			return total_performance_;
		}

		[[nodiscard]] inline auto preferred_node(const pid_t tid) const -> node_t {
			return table_[tid].preferred_node();
		}

		[[nodiscard]] inline auto reqs_per_node(const pid_t tid) const {
			return table_[tid].reqs_per_node();
		}

		friend auto operator<<(std::ostream & os, const tid_perf_table & t) -> std::ostream & {
			os << "Entries: " << t.table_.size() << '\n';

			tabulate::Table perf_table;

			perf_table.add_row({ "PID", "PPID", "RUNNING", "CPU", "NODE", "PREF.\nNODE", "PERFORMANCE\nW/O DECAY",
			                     "PERFORMANCE", "RELATIVE\nPERF. (%)", "CPU%", "OPS/S", "OPS/B", "AV. LAT",
			                     "NUMA\nSCORE" });

			for (const auto & [tid, per] : t.table_) {
				const auto & cpu  = system_info::pinned_cpu_from_tid(tid);
				const auto & node = system_info::node_from_cpu(cpu);

				const auto & pid          = system_info::pid_from_tid(tid);
				const auto & run          = utils::string::to_string(per.running());
				const auto & cpu_str      = utils::string::to_string(system_info::cpu_from_tid(tid));
				const auto & node_str     = utils::string::to_string(system_info::node_from_tid(tid));
				const auto & raw_perf     = utils::string::to_string(t.raw_performance(tid), 0);
				const auto & performance  = utils::string::to_string(t.performance(tid), 0);
				const auto & rel_perf     = utils::string::percentage(t.rel_performance(tid), 0);
				const auto & cpu_percent  = utils::string::percentage(system_info::cpu_use(tid), 0);
				const auto & ops_per_s    = utils::string::to_string(per.ops_per_s(node), 0);
				const auto & ops_per_byte = utils::string::to_string(per.ops_per_byte(node));
				const auto & av_latency   = utils::string::to_string(per.av_latency(node));
				const auto & numa_score   = utils::string::to_string(system_info::numa_score(pid), 2);

				perf_table.add_row({ std::to_string(tid), std::to_string(pid), run, cpu_str, node_str,
				                     std::to_string(per.preferred_node()), raw_perf, performance, rel_perf, cpu_percent,
				                     ops_per_s, ops_per_byte, av_latency, numa_score });
			}

			perf_table.format().hide_border().font_align(tabulate::FontAlign::right);
			perf_table.print(os);

			os << '\n';

			if (verbose::print_with_lvl(verbose::LVL4)) {
				for (const auto & [tid, per] : t.table_) {
					os << "TID " << tid << " perf:" << '\n';
					os << per;
				}
			}

			return os;
		}
	};
} // namespace performance

#endif /* end of include guard: THANOS_TID_PERF_TABLE_HPP */
