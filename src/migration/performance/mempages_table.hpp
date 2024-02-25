/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEMPAGES_TABLE_HPP
#define THANOS_MEMPAGES_TABLE_HPP

#include <algorithm>   // for fill, max_element, min_...
#include <numeric>     // for accumulate
#include <ostream>     // for operator<<, ostream
#include <string>      // for string, operator+, to_s...
#include <sys/types.h> // for size_t, pid_t
#include <tuple>       // for tie, _Swallow_assign
#include <type_traits> // for add_const<>::type
#include <utility>     // for pair, make_pair
#include <variant>     // for variant
#include <vector>      // for vector, vector<>::const...

#include "migration/utils/mem_sample.hpp"    // for memory_sample_t
#include "samples/perf_event/perf_event.hpp" // for minimum_latency
#include "system_info/system_info.hpp"       // for num_of_nodes, node_from...
#include "tabulate/tabulate.hpp"             // for Table, Format, FontAlign
#include "utils/string.hpp"                  // for to_string, percentage
#include "utils/types.hpp"                   // for real_t, addr_t, node_t

namespace performance {
	namespace memtable_details {
		class row {
		private:
			static constexpr auto SAMPLES_ENOUGH_INFO = 10;

			mutable bool ratios_computed_ = false;

			size_t samples_count_{};

			size_t age_{};

			mutable std::vector<real_t> ratios_{};

			std::vector<size_t> raw_accesses_{};  // Accesses from each node (raw count)
			std::vector<real_t> node_accesses_{}; // Accesses from each node (with aging)

			std::vector<lat_t> av_latencies_{};
			std::vector<req_t> av_latencies_ctr_{};

			lat_t av_latency_     = 0;
			req_t av_latency_ctr_ = 0;

			pid_t  last_pid_  = -1;
			node_t last_node_ = -1;

			inline void compute_ratios() const {
				const auto total_accesses = std::accumulate(raw_accesses_.begin(), raw_accesses_.end(), size_t());

				if (total_accesses == 0) { return; }

				for (const auto & node : system_info::nodes()) {
					ratios_[node] = static_cast<real_t>(raw_accesses_[node]) / static_cast<real_t>(total_accesses);
				}

				ratios_computed_ = true;
			}

		public:
			row() = delete;

			row(const pid_t last_pid, const node_t last_node) :
			    ratios_(system_info::max_node() + 1, 0),
			    raw_accesses_(system_info::max_node() + 1, 0),
			    node_accesses_(system_info::max_node() + 1, 0),
			    av_latencies_(system_info::max_node() + 1, 0),
			    av_latencies_ctr_(system_info::max_node() + 1, 0),
			    last_pid_(last_pid),
			    last_node_(last_node) {
			}

			[[nodiscard]] static inline auto samples_enough_info() {
				return SAMPLES_ENOUGH_INFO;
			}

			inline void clear() {
				std::fill(node_accesses_.begin(), node_accesses_.end(), 0);
				std::fill(raw_accesses_.begin(), raw_accesses_.end(), 0);
				std::fill(ratios_.begin(), ratios_.end(), 0);
				std::fill(av_latencies_.begin(), av_latencies_.end(), samples::minimum_latency);
				std::fill(av_latencies_ctr_.begin(), av_latencies_ctr_.end(), 0.0);

				av_latency_     = {};
				av_latency_ctr_ = {};

				ratios_computed_ = false;

				samples_count_ = {};

				age_ = {};
			}

			inline void add_data(const memory_sample_t & sample, const real_t aging_factor) {
				const auto node = sample.page_node();

				++samples_count_;

				++raw_accesses_[node];

				node_accesses_[node] += static_cast<real_t>(sample.reqs()) * aging_factor;

				const auto latency = sample.latency();

				av_latencies_[node] =
				    (av_latencies_[node] * av_latencies_ctr_[node] + latency) / (av_latencies_ctr_[node] + 1);
				++av_latencies_ctr_[node];

				av_latency_ = (av_latency_ * av_latency_ctr_ + latency) / (av_latency_ctr_ + 1);
				++av_latency_ctr_;

				last_pid_  = sample.tid();
				last_node_ = system_info::node_from_cpu(sample.cpu());

				ratios_computed_ = false;
			}

			[[nodiscard]] inline auto enough_info() const {
				return std::cmp_greater(samples_count_, SAMPLES_ENOUGH_INFO);
			}

			[[nodiscard]] inline auto samples_count() const {
				return samples_count_;
			}

			[[nodiscard]] inline auto age() const {
				return age_;
			}

			inline void increase_age() {
				ratios_computed_ = false;
				++age_;
			}

			[[nodiscard]] inline auto raw_accesses() const -> const auto & {
				return raw_accesses_;
			}

			[[nodiscard]] inline auto raw_accesses(const node_t node) const {
				return raw_accesses_[node];
			}

			[[nodiscard]] inline auto node_accesses() const -> const auto & {
				return node_accesses_;
			}

			[[nodiscard]] inline auto node_accesses(const node_t node) const {
				return node_accesses_[node];
			}

			[[nodiscard]] inline auto node_accesses() {
				return node_accesses_;
			}

			[[nodiscard]] inline auto preferred_node() const -> node_t {
				return static_cast<node_t>(std::max_element(node_accesses_.begin(), node_accesses_.end()) -
				                           node_accesses_.begin());
			}

			[[nodiscard]] inline auto reqs_per_node() {
				return node_accesses_;
			}

			[[nodiscard]] inline auto reqs_per_node() const -> const auto & {
				return node_accesses_;
			}

			[[nodiscard]] inline auto ratios() const -> const auto & {
				if (!ratios_computed_) { compute_ratios(); }
				return ratios_;
			}

			[[nodiscard]] inline auto ratio(const node_t node) const {
				if (!ratios_computed_) { compute_ratios(); }
				return ratios_[node];
			}

			[[nodiscard]] inline auto av_latency() const {
				return av_latency_;
			}

			[[nodiscard]] inline auto av_latency(const node_t node) const {
				return av_latencies_[node];
			}

			[[nodiscard]] inline auto av_latencies() const -> const auto & {
				return av_latencies_;
			}

			[[nodiscard]] inline auto last_pid() const {
				return last_pid_;
			}

			[[nodiscard]] inline auto last_node() const {
				return last_node_;
			}

			friend auto operator<<(std::ostream & os, const row & row) -> std::ostream & {
				for (const auto & node : system_info::nodes()) {
					os << row.node_accesses_[node] << " ";
				}
				return os;
			}
		};
	} // namespace memtable_details

	class mempages_table {
	private:
		fast_umap<addr_t, memtable_details::row> table_ = {};

		req_t accesses_   = 0;
		lat_t av_latency_ = samples::minimum_latency;

		std::vector<lat_t> node_latencies_{};
		std::vector<req_t> node_accesses_{};

	public:
		mempages_table() noexcept :
		    node_latencies_(system_info::max_node() + 1, samples::minimum_latency),
		    node_accesses_(system_info::max_node() + 1, 0) {
		}

		[[nodiscard]] inline auto begin() const {
			return table_.begin();
		}

		[[nodiscard]] inline auto end() const {
			return table_.end();
		}

		[[nodiscard]] inline auto begin() {
			return table_.begin();
		}

		[[nodiscard]] inline auto end() {
			return table_.end();
		}

		[[nodiscard]] inline auto size() const {
			return table_.size();
		}

		[[nodiscard]] static inline auto threshold_enough_info() {
			return memtable_details::row::samples_enough_info();
		}

		[[nodiscard]] inline auto pages_with_enough_info() const {
			return std::accumulate(table_.begin(), table_.end(), size_t(), [&](const auto & acc, const auto & el) {
				const auto & [addr, info] = el;
				return acc + (info.enough_info() ? 1 : 0);
			});
		}

		inline void clear_it() {
			if (memory_info::fake_thp_enabled()) {
				const auto LAST_SIZE = table_.size();
				// Clear the table completely
				table_.clear();
				// Preallocate to save some time
				table_.reserve(LAST_SIZE);
			} else {
				// Clear non-valid contents
				std::set<addr_t> to_remove;

				for (auto & [addr, info] : table_) {
					if (!memory_info::contains(addr)) { to_remove.insert(addr); }
				}

				for (const auto & addr : to_remove) {
					table_.erase(addr);
				}

				accesses_   = 0;
				av_latency_ = samples::minimum_latency;

				std::fill(node_latencies_.begin(), node_latencies_.end(), samples::minimum_latency);
				std::fill(node_accesses_.begin(), node_accesses_.end(), size_t());
			}
		}

		[[nodiscard]] inline auto find(const addr_t page) {
			return table_.find(page);
		}

		inline void remove_entry(const addr_t page) {
			table_.erase(page);
		}

		inline void add_data(const memory_sample_t & sample, real_t aging_factor = 1.0) {
			const auto page = sample.page();

			auto table_it = table_.find(page);
			if (table_it == table_.end()) { // = !contains(). We init the entry if it doesn't exist
				std::tie(table_it, std::ignore) = table_.insert({ page, { sample.tid(), sample.page_node() } });
			}

			auto & info = table_it->second;
			info.add_data(sample, aging_factor);

			const auto node    = sample.page_node();
			const auto latency = sample.latency();
			const auto reqs    = sample.reqs();

			node_latencies_[node] =
			    (node_accesses_[node] * node_latencies_[node] + latency * reqs) / (node_accesses_[node] + reqs);
			node_accesses_[node] += reqs;

			av_latency_ = (av_latency_ * accesses_ + latency * reqs) / (accesses_ + reqs);
			accesses_ += reqs;
		}

		[[nodiscard]] inline auto node_min_av_latency() const {
			return static_cast<node_t>(std::min_element(node_latencies_.begin(), node_latencies_.end()) -
			                           node_latencies_.begin());
		}

		[[nodiscard]] inline auto node_max_av_latency() const {
			return static_cast<node_t>(std::max_element(node_latencies_.begin(), node_latencies_.end()) -
			                           node_latencies_.begin());
		}

		[[nodiscard]] inline auto node_av_latencies() {
			return node_latencies_;
		}

		[[nodiscard]] inline auto node_av_latencies() const -> const auto & {
			return node_latencies_;
		}

		[[nodiscard]] inline auto av_latency(const node_t node) const {
			return node_latencies_[node];
		}

		[[nodiscard]] inline auto av_latency() const {
			return av_latency_;
		}

		[[nodiscard]] inline auto ratios(const addr_t page) const {
			return table_.at(page).ratios();
		}

		[[nodiscard]] inline auto node(const addr_t page) const {
			return table_.at(page).last_node();
		}

		[[nodiscard]] inline auto preferred_node(const addr_t page) const {
			return table_.at(page).preferred_node();
		}

		[[nodiscard]] inline auto reqs_per_node(const addr_t page) {
			return table_.at(page).reqs_per_node();
		}

		[[nodiscard]] inline auto reqs_per_node(const addr_t page) const -> const auto & {
			const auto & row = table_.at(page);
			return row.reqs_per_node();
		}

		[[nodiscard]] inline auto av_latency(const addr_t page) const {
			return table_.at(page).av_latency();
		}

		[[nodiscard]] inline auto av_latency(const addr_t page, const node_t node) const {
			return table_.at(page).av_latency(node);
		}

		[[nodiscard]] inline auto rel_latency(const addr_t page) const {
			return av_latency(page) * 100 / av_latency_;
		}

		[[nodiscard]] inline auto last_pid_to_access(const addr_t page) const {
			return table_.at(page).last_pid();
		}

		friend auto operator<<(std::ostream & os, const mempages_table & t) -> std::ostream & {
			os << "Entries: " << t.table_.size() << ". Global av. lat: " << utils::string::to_string(t.av_latency_, 2)
			   << '\n';

			tabulate::Table node_latencies_table;
			node_latencies_table.add_row({ "NODE", "AV. LATENCY", "AV. LATENCY\nNORMALISED (%)" });
			for (const auto & node : system_info::nodes()) {
				node_latencies_table.add_row({ std::to_string(node), utils::string::to_string(t.node_latencies_[node]),
				                               utils::string::percentage(t.node_latencies_[node], t.av_latency_) });
			}
			node_latencies_table.format().hide_border().font_align(tabulate::FontAlign::right);
			node_latencies_table.print(os);
			os << '\n';

			tabulate::Table mem_table;

			mem_table.add_row({ "ADDRESS", "THP START", "THP END", "NODE", "LAST PID", "SAMPLES", "AV. LATENCY",
			                    "REL. LATENCY\nSYSTEM (%)", "AV. LATENCY\nPER NODE", "ACCESSES\nPER NODE",
			                    "RELATIVE\nACCESSES (%)" });

			for (const auto & [page, row] : t.table_) {
				auto start = page;
				auto end   = page + memory_info::pagesize;

				if (memory_info::fake_thp_enabled()) {
					const auto thp_opt = memory_info::fake_thp_from_address(start);

					if (thp_opt.has_value()) {
						const auto & thp = thp_opt.value().get();

						start = thp.start();
						end   = thp.end();
					}
				}

				const auto & address     = utils::string::to_string_hex(page);
				const auto & start_str   = utils::string::to_string_hex(start);
				const auto & end_str     = utils::string::to_string_hex(end);
				const auto & src_node    = utils::string::to_string(t.node(page), 0);
				const auto & pid         = std::to_string(t.last_pid_to_access(page));
				const auto & samples     = utils::string::to_string(row.samples_count(), 0);
				const auto & av_latency  = utils::string::to_string(row.av_latency(), 0);
				const auto & rel_latency = utils::string::to_string(t.rel_latency(page), 0);

				std::string av_latency_by_node;
				std::string accesses;
				std::string rel_accesses;
				for (const auto & node : system_info::nodes()) {
					av_latency_by_node += utils::string::to_string(row.av_latency(node), 0) + " ";
					accesses += utils::string::to_string(row.node_accesses(node), 0) + " ";
					rel_accesses += utils::string::percentage(row.ratio(node), 0) + " ";
				}

				mem_table.add_row({ address, start_str, end_str, src_node, pid, samples, av_latency, rel_latency,
				                    av_latency_by_node, accesses, rel_accesses });
			}
			mem_table.format().hide_border().font_align(tabulate::FontAlign::right);
			mem_table.print(os);
			os << '\n';

			return os;
		}
	};
} // namespace performance

#endif /* end of include guard: THANOS_MEMPAGES_TABLE_HPP */
