/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_THREAD_STRATEGY_HPP
#define THANOS_THREAD_STRATEGY_HPP

#include <algorithm>   // for find_if
#include <functional>  // for greater
#include <iostream>    // for operator<<, basi...
#include <iterator>    // for advance
#include <map>         // for _Rb_tree_const_i...
#include <numa.h>      // for numa_distance
#include <numeric>     // for accumulate
#include <ranges>      // for ranges::iota_view...
#include <set>         // for set, set<>::iter...
#include <string>      // for char_traits, ope...
#include <sys/types.h> // for pid_t, size_t
#include <utility>     // for move, cmp...
#include <vector>      // for vector, vector<>...

#include "migration/migration_cell.hpp"             // for migration_cell
#include "migration/migration_var.hpp"              // for perf_table, last...
#include "migration/performance/performance.hpp"    // for PERFORMANCE_INVA...
#include "migration/performance/rm3d.hpp"           // for rm3d
#include "migration/performance/tid_perf_table.hpp" // for tid_perf_table
#include "migration/tickets.hpp"                    // for tickets_t, ticke...
#include "system_info/system_info.hpp"              // for is_migratable
#include "utils/arithmetic.hpp"                     // for rnd, sgn
#include "utils/string.hpp"                         // for to_string
#include "utils/types.hpp"                          // for real_t, node_t
#include "utils/verbose.hpp"                        // for DEFAULT_LVL, lvl

namespace migration::thread {

	[[nodiscard]] inline auto is_under_performance(const pid_t pid) -> bool {
		return perf_table.rel_performance(pid) < PERF_THRESHOLD;
	}

	template<template<typename...> typename Iterable>
	[[nodiscard]] static auto select_migration(const Iterable<migration_cell> & migrations,
	                                           const bool                       randomness = false) -> migration_cell {
		// Generate random numbers for each candidate
		map<tickets_val_t, migration_cell, std::greater<>> candidates;

		for (const auto & mig : migrations) {
			const auto tickets = mig.tickets().value();
			const auto random  = randomness ? utils::arithmetic::rnd(tickets_val_t(), tickets) : tickets;

			candidates.insert({ random, mig });
		}

		const auto & [val, mig_cell] = *candidates.begin();

		return mig_cell;
	}

	[[nodiscard]] inline auto tickets_rm3d(const pid_t pid, const node_t src_node, const node_t dst_node) -> tickets_t {
		const auto & perf = perf_table.get_rm3d(pid);

		if (perf[dst_node] == performance::PERFORMANCE_INVALID_VALUE) { return TICKETS_MEM_CELL_NO_DATA; }
		if (perf[dst_node] < perf[src_node]) { return TICKETS_MEM_CELL_WORSE; }
		return TICKETS_MEM_CELL_BETTER;
	}

	[[nodiscard]] inline auto tickets_pref_node(const pid_t pid, const node_t dst_node) -> tickets_t {
		const auto pref_node = perf_table.preferred_node(pid);

		tickets_t tickets(TICKETS_PREF_NODE.value() * static_cast<real_t>(system_info::local_distance()) /
		                      static_cast<real_t>(numa_distance(dst_node, pref_node)),
		                  dst_node == pref_node ? TICKETS_PREF_NODE.mask() : tickets_mask_t(0));

		return tickets;
	}

	[[nodiscard]] inline auto tickets_under_performance(const pid_t pid) -> tickets_t {
		return is_under_performance(pid) ? TICKETS_UNDER_PERF : tickets_t();
	}

	[[nodiscard]] inline auto tickets_free_core(const cpu_t dst_cpu) -> tickets_t {
		return system_info::is_cpu_free(dst_cpu) ? TICKETS_FREE_CORE : tickets_t();
	}

	[[nodiscard]] inline auto tickets_free_core_in_node(const node_t dst_node) -> tickets_t {
		return system_info::is_cpu_free_in_node(dst_node) ? TICKETS_FREE_CORE : tickets_t();
	}

	[[nodiscard]] inline auto tickets_node(const pid_t pid, const node_t src_node, const node_t dst_node) -> tickets_t {
		return tickets_rm3d(pid, src_node, dst_node) + tickets_pref_node(pid, dst_node) +
		       tickets_free_core_in_node(dst_node) + tickets_under_performance(pid);
	}

	[[nodiscard]] inline auto tickets_node(const pid_t pid, const node_t dst_node) -> tickets_t {
		const auto src_node = system_info::pinned_node_from_tid(pid);
		return tickets_node(pid, src_node, dst_node);
	}

	[[nodiscard]] inline auto tickets_cpu(const pid_t pid, const cpu_t src_cpu, const cpu_t dst_cpu) -> tickets_t {
		const auto src_node = system_info::node_from_cpu(src_cpu);
		const auto dst_node = system_info::node_from_cpu(dst_cpu);

		return tickets_rm3d(pid, src_node, dst_node) + tickets_pref_node(pid, dst_node) + tickets_free_core(dst_cpu) +
		       tickets_under_performance(pid);
	}

	[[nodiscard]] inline auto tickets_cpu(const pid_t pid, const cpu_t dst_cpu) -> tickets_t {
		const auto src_cpu = system_info::pinned_cpu_from_tid(pid);
		return tickets_node(pid, src_cpu, dst_cpu);
	}

	[[nodiscard]] inline auto mutate_ticket(const tickets_t tickets, const tickets_val_t range, const real_t diff)
	    -> tickets_t {
		const auto incr = utils::arithmetic::rnd(tickets_val_t(), range);
		const auto sign = utils::arithmetic::sgn(diff);

		return tickets_t(tickets.value() * (1 + sign * incr), tickets.mask());
	}

	void mutate_tickets(const migration_cell & migrs, const real_t range = 0.05,
	                    const bool print = (verbose::print_with_lvl(verbose::DEFAULT_LVL))) {
		const auto mask = migrs.tickets().mask();

		for (const auto & migr : migrs) {
			const auto & current_per = thread::perf_table.performance(migr.tid());
			const auto   diff        = current_per / migr.prev_perf();
			const auto   improvement = 100 * (diff - 1);

			if (print) {
				std::cout << "\tTID: " << migr.tid() << ". Last perf: " << migr.prev_perf()
				          << ". Current: " << current_per << ". Improvement: " << improvement << "%" << '\n';
			}

			if ((mask & tickets_mask::TICKETS_MEM_CELL_WORSE_MASK) != 0) {
				const auto previous_value = TICKETS_MEM_CELL_WORSE;
				TICKETS_MEM_CELL_WORSE    = mutate_ticket(TICKETS_MEM_CELL_WORSE, range, improvement);

				if (print) {
					std::cout << "\tPrevious value of TICKETS_MEM_CELL_WORSE:" << previous_value << "."
					          << " New value: " << TICKETS_MEM_CELL_WORSE << '\n';
				}
			}

			if ((mask & tickets_mask::TICKETS_MEM_CELL_NO_DATA_MASK) != 0) {
				const auto previous_value = TICKETS_MEM_CELL_NO_DATA;
				TICKETS_MEM_CELL_NO_DATA  = mutate_ticket(previous_value, range, improvement);

				if (print) {
					std::cout << "\tPrevious value of TICKETS_MEM_CELL_NO_DATA:" << previous_value << "."
					          << " New value: " << TICKETS_MEM_CELL_NO_DATA << '\n';
				}
			}

			if ((mask & tickets_mask::TICKETS_MEM_CELL_BETTER_MASK) != 0) {
				const auto previous_value = TICKETS_MEM_CELL_BETTER;
				TICKETS_MEM_CELL_BETTER   = mutate_ticket(previous_value, range, improvement);

				if (print) {
					std::cout << "\tPrevious value of TICKETS_MEM_CELL_BETTER:" << previous_value << "."
					          << " New value: " << TICKETS_MEM_CELL_BETTER << '\n';
				}
			}

			if ((mask & tickets_mask::TICKETS_FREE_CORE_MASK) != 0) {
				const auto previous_value = TICKETS_FREE_CORE;
				TICKETS_FREE_CORE         = mutate_ticket(previous_value, range, improvement);

				if (print) {
					std::cout << "\tPrevious value of TICKETS_FREE_CORE:" << previous_value << "."
					          << " New value: " << TICKETS_FREE_CORE << '\n';
				}
			}

			if ((mask & tickets_mask::TICKETS_PREF_NODE_MASK) != 0) {
				const auto previous_value = TICKETS_PREF_NODE;
				TICKETS_PREF_NODE         = mutate_ticket(previous_value, range, improvement);

				if (print) {
					std::cout << "\tPrevious value of TICKETS_PREF_NODE:" << previous_value << "."
					          << " New value: " << TICKETS_PREF_NODE << '\n';
				}
			}

			if ((mask & tickets_mask::TICKETS_THREAD_UNDER_PERF_MASK) != 0) {
				const auto previous_value = TICKETS_UNDER_PERF;
				TICKETS_UNDER_PERF        = mutate_ticket(previous_value, range, improvement);

				if (print) {
					std::cout << "\tPrevious value of TICKETS_THREAD_UNDER_PERF:" << previous_value << "."
					          << " New value: " << TICKETS_UNDER_PERF << '\n';
				}
			}
		}
	}

	inline void mutate_tickets(const real_t range = 0.05,
	                           const bool   print = (verbose::print_with_lvl(verbose::DEFAULT_LVL))) {
		for (const auto & migrs : thread::last_migrations) {
			mutate_tickets(migrs, range, print);
		}

		std::cout << std::defaultfloat;
	}

	template<class Iterable>
	[[nodiscard]] inline auto n_threads_involved(const Iterable & migrations) -> size_t {
		static_assert(std::is_same_v<typename Iterable::value_type, migration_cell>);
		return std::accumulate(migrations.begin(), migrations.end(), 0,
		                       [](auto n, const auto & m) { return n + m.tids_involved().size(); });
	}

	[[nodiscard]] inline auto compare_last_migrations(const bool print = false) -> real_t {
		real_t total = 0;

		if (print) { std::cout << "Last migrations:" << '\n'; }

		for (const auto & migrs : last_migrations) {
			for (const auto & migr : migrs.migrations()) {
				const auto & current_per = perf_table.performance(migr.tid());
				const auto   diff        = current_per / migr.prev_perf();
				const auto   improvement = 100 * (diff - 1);
				if (print) {
					std::cout << "\tTID: " << migr.tid()
					          << ". Last perf: " << utils::string::to_string(migr.prev_perf())
					          << ". Current: " << utils::string::to_string(current_per)
					          << ". Improvement: " << utils::string::to_string(improvement) << "%" << '\n';
				}
				total += diff;
			}
		}

		if (print) { std::cout << "Profit of last migrations: " << total << '\n'; }

		return total;
	}

	inline void undo_last_migrations(const bool only_bad_migr = false) {
		if (last_migrations.empty()) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << "No migrations to undo..." << '\n'; }
			return;
		}
		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << "Undoing last migrations..." << '\n'; }
		for (auto & undo_migr : last_migrations) {
			std::vector<real_t> perfs;

			if (only_bad_migr) {
				for (const auto & migr : undo_migr.migrations()) {
					perfs.emplace_back(perf_table.performance(migr.tid()));
				}
			}

			if (!only_bad_migr || undo_migr.balance(perfs) < 0) {
				for (auto & migr : undo_migr.migrations()) {
					migr.swap();
				}

				if (undo_migr.migrate()) {
					++migration::thread::total_migrations_undone;
				} else if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Error undoing migrations" << '\n';
				}
			}
		}
		last_migrations.clear();
	}

	namespace mapping {
		class thread_t {
		private:
			pid_t tid_;

			performance::rm3d rm3d_;

			node_t src_node_;
			node_t dst_node_;

		public:
			thread_t(const pid_t tid, performance::rm3d rm3d, const node_t node) :
			    tid_(tid), rm3d_(std::move(rm3d)), src_node_(node), dst_node_(node) {
			}

			[[nodiscard]] inline auto tid() const {
				return tid_;
			}

			[[nodiscard]] inline auto src_node() const {
				return src_node_;
			}

			[[nodiscard]] inline auto dst_node() const {
				return dst_node_;
			}

			inline void dst_node(const node_t node) {
				dst_node_ = node;
			}

			[[nodiscard]] inline auto expected_perf(const node_t dst_node) const -> real_t {
				const auto & node_reqs = rm3d_.scaled_node_reqs();

				// Compute the estimated av. latency in the destination node
				real_t est_av_lat = 0.0;
				req_t  total_reqs = 0;

				for (const auto & node : system_info::nodes()) {
					est_av_lat += static_cast<real_t>(node_reqs[node]) * perf_table.av_latency(dst_node, node);
					total_reqs += node_reqs[node];
				}
				est_av_lat /= static_cast<real_t>(total_reqs);

				// Assume that Ops/s improves as much as latency is reduced
				const auto ops_per_s =
				    rm3d_.ops_per_second(src_node_) * static_cast<real_t>(rm3d_.av_latency(src_node_)) / est_av_lat;
				// Assume that Ops/B stays the same...
				const auto ops_per_b = rm3d_.ops_per_byte(src_node_);

				const auto perf = performance::rm3d::calc_perf(ops_per_s, ops_per_b, est_av_lat);

				return perf;
			}

			[[nodiscard]] inline auto expected_perf() const -> real_t {
				return expected_perf(dst_node_);
			}

			[[nodiscard]] inline auto expected_tickets(const node_t dst_node) const -> tickets_t {
				return tickets_rm3d(tid_, src_node_, dst_node) + tickets_pref_node(tid_, dst_node);
			}

			[[nodiscard]] inline auto expected_tickets() const -> tickets_t {
				return expected_tickets(dst_node_);
			}
		};

		class schedule_t {
		private:
			std::vector<thread_t>    threads_;
			umap<node_t, set<pid_t>> node_tid_map_ = {};

			mutable bool tickets_computed_ = false;
			mutable bool perf_computed_    = false;

			mutable tickets_val_t expected_tickets_ = {};
			mutable real_t        expected_perf_    = {};

			inline void compute_expected_tickets() const {
				expected_tickets_ = std::accumulate(threads_.begin(), threads_.end(), tickets_val_t(0.0),
				                                    [](const auto accum, const auto & thread) {
					                                    return accum + thread.expected_tickets().value();
				                                    });
				tickets_computed_ = true;
			}

			inline void compute_expected_performance() const {
				expected_perf_ = std::accumulate(threads_.begin(), threads_.end(), real_t(0.0),
				                                 [](const auto accum, const auto & thread) {
					                                 return accum + thread.expected_perf();
				                                 });
				perf_computed_ = true;
			}

		public:
			schedule_t() = delete;

			explicit schedule_t(std::vector<thread_t> threads) : threads_(std::move(threads)) {
				for (const auto & thread : threads_) {
					if (!system_info::is_idle(thread.tid())) { node_tid_map_[thread.dst_node()].insert(thread.tid()); }
				}
			}

			[[nodiscard]] inline auto threads() const -> const auto & {
				return threads_;
			}

			[[nodiscard]] inline auto threads() -> auto & {
				return threads_;
			}

			[[nodiscard]] inline auto threads_in_node(const node_t node) const -> const auto & {
				return node_tid_map_.at(node);
			}

			[[nodiscard]] inline auto threads_in_node(const node_t node) {
				return node_tid_map_.at(node);
			}

			[[nodiscard]] inline auto n_threads_in_node(const node_t node) const -> size_t {
				return node_tid_map_.at(node).size();
			}

			inline auto migrate(const pid_t tid, const node_t dst) -> bool {
				tickets_computed_ = false;
				perf_computed_    = false;

				for (auto & thread : threads_) {
					if (std::cmp_equal(thread.tid(), tid)) {
						const auto prev_node = thread.dst_node();
						thread.dst_node(dst);
						if (!system_info::is_idle(thread.tid())) {
							node_tid_map_[prev_node].erase(thread.tid());
							node_tid_map_[dst].insert(thread.tid());
						}
						return true;
					}
				}

				return false;
			}

			inline auto interchange(const pid_t tid_1, const pid_t tid_2) -> bool {
				tickets_computed_ = false;
				perf_computed_    = false;

				auto t1_it = std::find_if(threads_.begin(), threads_.end(),
				                          [&](const thread_t & thread) { return tid_1 == thread.tid(); });
				auto t2_it = std::find_if(threads_.begin(), threads_.end(),
				                          [&](const thread_t & thread) { return tid_2 == thread.tid(); });

				if (t1_it == threads_.end() || t2_it == threads_.end()) { return false; }

				auto & t1 = *t1_it;
				auto & t2 = *t2_it;

				const auto t1_prev_node = t1.dst_node();
				const auto t2_prev_node = t2.dst_node();

				t1.dst_node(t2_prev_node);
				t2.dst_node(t1_prev_node);

				node_tid_map_[t1_prev_node].erase(t1.tid());
				node_tid_map_[t1.dst_node()].insert(t1.tid());

				node_tid_map_[t2_prev_node].erase(t2.tid());
				node_tid_map_[t2.dst_node()].insert(t2.tid());

				return true;
			}

			[[nodiscard]] auto neighbour(const size_t max_mutations = 1) const {
				auto neigh = *this;

				set<pid_t> migrated_tids;

				auto & threads = neigh.threads();

				for ([[maybe_unused]] const auto mutation : std::ranges::iota_view(size_t(), max_mutations)) {
					const auto idx = utils::arithmetic::rnd(size_t(), threads.size());

					auto & thread = threads[idx];

					if (!system_info::is_migratable(thread.tid()) || system_info::is_idle(thread.tid())) { continue; }

					const auto tid = thread.tid();

					auto dst_node = utils::arithmetic::rnd<node_t>(size_t(), system_info::max_node());

					while (dst_node == thread.dst_node()) {
						dst_node = utils::arithmetic::rnd<node_t>(size_t(), system_info::max_node());
					}

					// Free core: possible simple migration with a determined score
					if (std::cmp_less(neigh.n_threads_in_node(dst_node), system_info::num_of_cpus(dst_node))) {
						neigh.migrate(tid, dst_node);
						continue;
					}

					// Not free cores: get its TIDs info so a possible interchange can be planned
					const auto swap_tids = neigh.threads_in_node(dst_node);

					pid_t swap_tid = 0;

					bool migr_found = false;

					for ([[maybe_unused]] const auto attempt : std::ranges::iota_view(size_t(), swap_tids.size())) {
						const auto distance = utils::arithmetic::rnd(size_t(), swap_tids.size());
						auto       tids_it  = swap_tids.begin();
						std::ranges::advance(tids_it, distance, swap_tids.end());
						swap_tid = *tids_it;

						// Check if it is viable to migrate the other thread
						if (system_info::is_migratable(swap_tid)) {
							migr_found = true;
							break;
						}
					}

					if (migr_found) {
						neigh.interchange(tid, swap_tid);
						migrated_tids.insert(tid);
						migrated_tids.insert(swap_tid);
					}
				}

				return neigh;
			}

			[[nodiscard]] inline auto expected_tickets() const -> tickets_val_t {
				if (!tickets_computed_) { compute_expected_tickets(); }

				return expected_tickets_;
			}

			[[nodiscard]] inline auto expected_perf() const -> real_t {
				if (!perf_computed_) { compute_expected_performance(); }

				return expected_perf_;
			}

			[[nodiscard]] auto migrations() -> std::vector<migration_cell> {
				std::vector<migration_cell> mig;
				set<pid_t>                  migrated_tids;

				for (const auto & t1 : threads()) {
					if (std::cmp_equal(t1.src_node(), t1.dst_node()) || migrated_tids.contains(t1.tid())) { continue; }

					const auto pid  = system_info::pid_from_tid(t1.tid());
					const auto perf = perf_table.performance(t1.tid());

					simple_migration_cell mc(t1.tid(), pid, t1.dst_node(), t1.src_node(), true, perf);

					auto tickets = tickets_rm3d(t1.tid(), t1.src_node(), t1.dst_node());
					tickets += tickets_pref_node(t1.tid(), t1.dst_node());

					if (std::cmp_less(n_threads_in_node(t1.dst_node()), system_info::num_of_cpus(t1.dst_node()))) {
						tickets += TICKETS_FREE_CORE;
					}

					if (is_under_performance(t1.tid())) { tickets += TICKETS_UNDER_PERF; }

					// If there is a free core, use a simple migration
					if (std::cmp_less(n_threads_in_node(t1.dst_node()), system_info::cpus_per_node())) {
						mig.emplace_back(migration_cell({ mc }, tickets));
						migrated_tids.insert(t1.tid());
						node_tid_map_[t1.dst_node()].insert(t1.tid());
						continue;
					}

					// Not a free core in destination node: get its TIDs info so a possible interchange can be planned
					// We will choose the TID which generates the highest number of tickets
					const auto & tids = system_info::non_idle_tids_from_node(t1.dst_node());

					bool      migr_found = false;
					pid_t     swap_tid   = 0;
					tickets_t swap_tickets;

					for (const auto & aux_tid : tids) {
						if (!system_info::is_migratable(aux_tid) || migrated_tids.contains(aux_tid)) { continue; }

						const auto & t2 = std::find_if(threads().begin(), threads().end(),
						                               [&](const auto & t) { return t.dst_node() == t1.src_node(); });

						if (t2 == threads().end()) { continue; }

						auto tickets_tid = tickets_rm3d(aux_tid, t1.dst_node(), t1.src_node()) +
						                   tickets_pref_node(aux_tid, t1.src_node()) +
						                   tickets_under_performance(aux_tid);

						// Check if it is the better candidate (has more tickets)
						if (tickets_tid > swap_tickets) {
							swap_tickets = tickets_tid;
							swap_tid     = aux_tid;
							migr_found   = true;
						}
					}

					if (migr_found) {
						tickets += swap_tickets;

						const auto swap_pid  = system_info::pid_from_tid(swap_tid);
						const auto swap_perf = perf_table.performance(swap_tid);

						simple_migration_cell mc_2(swap_tid, swap_pid, t1.src_node(), t1.dst_node(), true, swap_perf);
						mig.emplace_back(std::initializer_list<simple_migration_cell>{ mc, mc_2 }, tickets);

						migrated_tids.insert(t1.tid());
						migrated_tids.insert(swap_tid);

						// No need of updating threads per node as it is an interchange.
					}
				}

				return mig;
			}
		};
	} // namespace mapping

	// Interface class for thread migration strategy
	class Istrategy {
	public:
		Istrategy()                        = default;
		Istrategy(const Istrategy & strat) = default;
		Istrategy(Istrategy && strat)      = default;

		virtual ~Istrategy() = default;

		auto operator=(const Istrategy &) -> Istrategy & = default;
		auto operator=(Istrategy &&) -> Istrategy &      = default;

		[[nodiscard]] inline virtual auto migrate_to_nodes() const noexcept -> bool = 0;

		virtual void migrate() = 0;

	private:
		// Search first in CPUs of the "closest" nodes
		[[nodiscard]] static auto closest_less_busy_cpu(const cpu_t src_cpu, const size_t min_tids_per_cpu,
		                                                const bool ignore_idle) -> std::optional<cpu_t> {
			const auto nodes_by_distance = system_info::nodes_by_distance(system_info::node_from_cpu(src_cpu));

			auto min_tids = std::numeric_limits<size_t>::max();
			auto min_cpu  = src_cpu;

			// Search first in "closest" nodes
			for (const auto & dst_node : nodes_by_distance) {
				for (const auto & dst_cpu : system_info::cpus_from_node(dst_node)) {
					if (std::cmp_equal(src_cpu, dst_cpu)) { continue; }

					const auto dst_tids = system_info::tids_from_cpu(dst_cpu, ignore_idle).size();

					if (std::cmp_less(dst_tids, min_tids)) {
						min_cpu  = dst_cpu;
						min_tids = dst_tids;
						if (std::cmp_less(dst_tids, min_tids_per_cpu)) { return min_cpu; }
					}
				}
			}

			const auto src_tids = system_info::tids_from_cpu(src_cpu, ignore_idle).size();

			if (std::cmp_less(min_tids, src_tids)) { return min_cpu; }

			return std::nullopt;
		}

		// Search first in "closest" nodes
		[[nodiscard]] static auto closest_less_busy_node(const node_t src_node, const size_t min_tids_per_node,
		                                                 const bool ignore_idle) -> std::optional<node_t> {
			const auto & nodes_by_distance = system_info::nodes_by_distance(src_node);

			node_t min_node = src_node;
			size_t min_tids = std::numeric_limits<size_t>::max();

			// Search first in "closest" nodes
			for (const auto & dst_node : nodes_by_distance) {
				// Skip current node...
				if (std::cmp_equal(src_node, dst_node)) { continue; }

				const auto dst_tids = system_info::tids_from_node(dst_node, ignore_idle).size();

				if (std::cmp_less(dst_tids, min_tids)) {
					min_node = dst_node;
					min_tids = dst_tids;
					if (std::cmp_less(dst_tids, min_tids_per_node)) { return min_node; }
				}
			}

			const auto src_tids = system_info::tids_from_node(src_node, ignore_idle).size();

			if (std::cmp_less(min_tids, src_tids)) { return min_node; }

			return std::nullopt;
		}

		// "Less busy" does not mean "least busy"
		[[nodiscard]] static auto move_to_less_busy_cpu(const cpu_t src_cpu, const size_t min_tids_per_cpu,
		                                                const bool ignore_idle) -> std::vector<migration_cell> {
			auto tids = system_info::tids_from_cpu(src_cpu, ignore_idle);

			std::vector<migration_cell> migrations;

			bool migrated = false;

			do {
				const auto & tid = *(tids.begin());
				migrated         = false;

				const auto opt_min_cpu = closest_less_busy_cpu(src_cpu, min_tids_per_cpu, ignore_idle);

				if (!opt_min_cpu.has_value()) { break; }

				const auto min_cpu = opt_min_cpu.value();

				simple_migration_cell mc(tid, system_info::pid_from_tid(tid), min_cpu, src_cpu,
				                         perf_table.performance(tid));

				migration_cell migration({ mc }, TICKETS_FREE_CORE);

				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << "Balancing: " << migration << '\n'; }

				if (migration.migrate()) {
					migrations.emplace_back(migration);
					migrated = true;
				} else if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Error balancing threads" << '\n';
				}

				// Update TIDs list
				tids = system_info::tids_from_cpu(src_cpu, ignore_idle);
			} while (migrated && tids.size() > min_tids_per_cpu);

			return migrations;
		}

		// "Less busy" does not mean "least busy"
		[[nodiscard]] auto move_to_less_busy_node(const node_t src_node, const size_t min_tids_per_node,
		                                          const bool ignore_idle) const -> std::vector<migration_cell> {
			auto tids = system_info::tids_from_node(src_node, ignore_idle);

			std::vector<migration_cell> migrations;

			bool migrated = false;

			do {
				const auto & tid = *(tids.begin());
				migrated         = false;

				const auto opt_min_node = closest_less_busy_node(src_node, min_tids_per_node, ignore_idle);

				if (!opt_min_node.has_value()) { break; }

				const auto min_node = opt_min_node.value();

				simple_migration_cell mc(tid, system_info::pid_from_tid(tid), min_node, src_node, migrate_to_nodes(),
				                         perf_table.performance(tid));
				migration_cell        migration({ mc }, TICKETS_FREE_CORE);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << "Balancing: " << migration << '\n'; }
				if (migration.migrate()) {
					migrations.emplace_back(migration);
					migrated = true;
				} else if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Error balancing threads" << '\n';
				}

				// Update list of threads
				tids = system_info::tids_from_node(src_node, ignore_idle);
			} while (migrated && tids.size() > min_tids_per_node);

			return migrations;
		}

		[[nodiscard]] static auto balance_CPUs(const bool ignore_idle) -> std::vector<migration_cell> {
			const auto total_tids = system_info::tids(ignore_idle).size();

			// No threads -> no imbalance
			if (std::cmp_equal(total_tids, 0)) { return {}; }

			const auto min_tids_per_cpu = std::max(total_tids / system_info::num_of_cpus(), size_t(1));

			std::vector<migration_cell> migrations_done;
			for (const auto & cpu : system_info::cpus()) {
				auto tids = system_info::tids_from_cpu(cpu, ignore_idle);

				if (std::cmp_less_equal(tids.size(), min_tids_per_cpu)) { continue; }

				// If the CPU has more than "min_tids_per_cpu" thread running, try to move threads to free cores
				const auto migr = move_to_less_busy_cpu(cpu, min_tids_per_cpu, ignore_idle);

				migrations_done.insert(migrations_done.end(), migr.begin(), migr.end());
			}

			return migrations_done;
		}

		[[nodiscard]] auto balance_nodes(const bool ignore_idle) const -> std::vector<migration_cell> {
			const auto total_tids = system_info::tids(ignore_idle).size();

			// No threads -> no imbalance
			if (std::cmp_equal(total_tids, 0)) { return {}; }

			const auto min_tids_per_node = std::max(total_tids / system_info::num_of_nodes(), size_t(1));

			std::vector<migration_cell> migrations_done;

			for (const auto & node : system_info::nodes()) {
				auto tids = system_info::tids_from_node(node, ignore_idle);

				if (std::cmp_less_equal(tids.size(), min_tids_per_node)) { continue; }

				// If the node has more than [min_tids_per_node] threads running, try to move threads to free node
				const auto migr = move_to_less_busy_node(node, min_tids_per_node, ignore_idle);

				migrations_done.insert(migrations_done.end(), migr.begin(), migr.end());
			}

			return migrations_done;
		}

		[[nodiscard]] static auto pid_in_cpu_that_solve_imbalance(const map<pid_t, real_t> & pid_load_map,
		                                                          const cpu_t cpu, const real_t imbalance) {
			pid_t  best_pid  = 0;
			real_t best_load = std::numeric_limits<real_t>::max();

			for (const auto & [pid, load] : pid_load_map) {
				if (system_info::cpu_from_tid(pid) == cpu &&
				    std::abs(load - imbalance) < std::abs(best_load - imbalance)) {
					best_pid  = pid;
					best_load = load;
				}
			}

			return best_pid;
		}

		[[nodiscard]] static auto pid_in_node_that_solve_imbalance(const map<pid_t, real_t> & pid_load_map,
		                                                           const node_t node, const real_t imbalance) {
			pid_t  best_pid  = 0;
			real_t best_load = std::numeric_limits<real_t>::max();

			for (const auto & [pid, load] : pid_load_map) {
				if (system_info::node_from_tid(pid) == node &&
				    std::abs(load - imbalance) < std::abs(best_load - imbalance)) {
					best_pid  = pid;
					best_load = load;
				}
			}

			return best_pid;
		}

		[[nodiscard]] static auto balance_CPUs_load(const bool only_pinned) -> std::vector<migration_cell> {
			const auto pid_load_map = system_info::load_per_pid();

			const auto system_load = system_info::load_system(pid_load_map, only_pinned);
			const auto target_load =
			    std::max(real_t(1.0), system_load / static_cast<real_t>(system_info::num_of_cpus()));

			std::vector<migration_cell> migrations_done;

			bool balanced = false;

			while (!balanced) {
				// Compute load per cpu
				const auto cpu_load_map = system_info::load_per_cpu(pid_load_map, only_pinned);
				// Get the most and the least loaded CPUs and their loads
				const auto max_load_it  = std::ranges::max_element(cpu_load_map);
				const auto min_load_it  = std::ranges::min_element(cpu_load_map);

				const auto max_load = *max_load_it;
				const auto min_load = *min_load_it;

				const auto max_load_cpu = static_cast<cpu_t>(std::distance(cpu_load_map.begin(), max_load_it));
				const auto min_load_cpu = static_cast<cpu_t>(std::distance(cpu_load_map.begin(), min_load_it));

				// If the maximum load is under the target load, assume that no CPU is overloaded
				if (max_load < target_load) {
					balanced = true;
					continue;
				}

				// There is an overloaded CPU, try to move tasks to the least loaded CPU
				const auto imbalance = max_load - min_load;

				// Find the pid whose load is most similar to the imbalance
				const auto pid  = pid_in_cpu_that_solve_imbalance(pid_load_map, max_load_cpu, imbalance);
				const auto load = pid_load_map.at(pid);

				// If things are going to improve, do the balance
				if (std::abs((max_load - load) - (min_load + load)) < imbalance) {
					simple_migration_cell mc(pid, system_info::pid_from_tid(pid), min_load_cpu, max_load_cpu,
					                         perf_table.performance(pid));
					migration_cell        migration({ mc }, TICKETS_FREE_CORE);
					std::ignore = migration.migrate();
					migrations_done.emplace_back(migration);
				} else {
					// We cannot improve things :(
					balanced = true;
				}
			}

			return migrations_done;
		}

		[[nodiscard]] static auto balance_nodes_load(const bool only_pinned) -> std::vector<migration_cell> {
			const auto pid_load_map = system_info::load_per_pid();

			const auto system_load = system_info::load_system(pid_load_map, only_pinned);
			const auto target_load =
			    std::max(real_t(1.0), system_load / static_cast<real_t>(system_info::num_of_nodes()));

			std::vector<migration_cell> migrations_done;

			bool balanced = false;

			while (!balanced) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { system_info::print_system_status(); }

				// Compute load per node
				const auto node_load_map = system_info::load_per_node(pid_load_map, only_pinned);
				// Get the most and the least loaded nodes and their loads
				const auto max_load_it   = std::ranges::max_element(node_load_map);
				const auto min_load_it   = std::ranges::min_element(node_load_map);

				const auto max_load = *max_load_it;
				const auto min_load = *min_load_it;

				const auto max_load_node = static_cast<node_t>(std::distance(node_load_map.begin(), max_load_it));
				const auto min_load_node = static_cast<node_t>(std::distance(node_load_map.begin(), min_load_it));

				// If the maximum load is under the target load, assume that no node is overloaded
				if (max_load < target_load) {
					balanced = true;
					continue;
				}

				// There is an overloaded node, try to move tasks to the least loaded node
				const auto imbalance = max_load - min_load;

				// Find the pid whose load is most similar to the imbalance
				const auto pid  = pid_in_node_that_solve_imbalance(pid_load_map, max_load_node, imbalance);
				const auto load = pid_load_map.at(pid);

				// If things are going to improve, do the balance
				if (std::abs((max_load - load) - (min_load + load)) < imbalance) {
					simple_migration_cell mc(pid, system_info::pid_from_tid(pid), min_load_node, max_load_node,
					                         /* node migration = */ true, perf_table.performance(pid));
					migration_cell        migration({ mc }, TICKETS_FREE_CORE);
					std::ignore = migration.migrate();
					migrations_done.emplace_back(migration);
				} else {
					// We cannot improve things :(
					balanced = true;
				}
			}

			return migrations_done;
		}

	public:
		[[nodiscard]] auto balance(const bool ignore_idle) const -> std::vector<migration_cell> {
			// If this is a strategy to migrate to nodes...
			if (migrate_to_nodes()) {
				// ...pin threads to nodes...
				if (system_info::pin_threads_node(ignore_idle)) {
					// ... and balance workload
					return balance_nodes(ignore_idle);
				}

				// If pin to node didn't work...
				return {};
			}

			// Pin threads to CPUs...
			if (system_info::pin_threads_cpu(ignore_idle)) {
				// ... and balance workload
				return balance_CPUs(ignore_idle);
			}

			// Nothing worked, no migrations performed
			return {};
		}

		template<template<typename...> typename Iterable>
		[[nodiscard]] static auto best_pid_for_swap_cpu(const cpu_t src_cpu, const cpu_t dst_cpu,
		                                                const Iterable<pid_t> & migrated_pids = set<pid_t>())
		    -> std::optional<std::pair<pid_t, tickets_t>> {
			const auto tids = system_info::non_idle_tids_from_cpu(dst_cpu);

			pid_t     swap_tid = 0;
			tickets_t swap_tickets;

			for (const auto & aux_tid : tids) {
				if (!system_info::is_migratable(aux_tid)) { continue; }
				if (migrated_pids.contains(aux_tid)) { continue; }

				// Compute tickets
				auto tickets_tid = tickets_cpu(aux_tid, dst_cpu, src_cpu);

				// Check if it is the better candidate (has more tickets)
				if (tickets_tid > swap_tickets) {
					swap_tickets = tickets_tid;
					swap_tid     = aux_tid;
				}
			}

			if (std::cmp_equal(swap_tid, 0)) {
				// Could not find a valid TID for swap
				return std::nullopt;
			}

			return std::make_pair(swap_tid, swap_tickets);
		}


		template<template<typename...> typename Iterable>
		[[nodiscard]] static auto best_pid_for_swap_node(const node_t src_node, const node_t dst_node,
		                                                 const Iterable<pid_t> & migrated_pids = set<pid_t>())
		    -> std::optional<std::pair<pid_t, tickets_t>> {
			const auto tids = system_info::non_idle_tids_from_node(dst_node);

			pid_t     swap_tid = 0;
			tickets_t swap_tickets;

			for (const auto & aux_tid : tids) {
				if (!system_info::is_migratable(aux_tid)) { continue; }
				if (migrated_pids.contains(aux_tid)) { continue; }

				// Compute tickets
				auto tickets_tid = tickets_node(aux_tid, dst_node, src_node);

				// Check if it is the better candidate (has more tickets)
				if (tickets_tid > swap_tickets) {
					swap_tickets = tickets_tid;
					swap_tid     = aux_tid;
				}
			}

			if (std::cmp_equal(swap_tid, 0)) {
				// Could not find a valid TID for swap
				return std::nullopt;
			}

			return std::make_pair(swap_tid, swap_tickets);
		}
	};
} // namespace migration::thread

#endif /* end of include guard: THANOS_THREAD_STRATEGY_HPP */
