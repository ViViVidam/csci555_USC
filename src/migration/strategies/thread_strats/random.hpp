/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_RANDOM_HPP
#define THANOS_RANDOM_HPP

#include <algorithm>   // for min
#include <iostream>    // for operator<<, basi...
#include <iterator>    // for advance
#include <map>         // for operator==, _Rb_...
#include <set>         // for set, set<>::iter...
#include <string>      // for operator<<, char...
#include <sys/types.h> // for size_t, pid_t
#include <utility>     // for pair, tuple_elem...
#include <vector>      // for vector

#include "migration/migration_cell.hpp"             // for migration_cell
#include "migration/migration_var.hpp"              // for perf_table, last...
#include "migration/performance/performance.hpp"    // for NEGLIGIBLE_PERFO...
#include "migration/performance/tid_perf_table.hpp" // for tid_perf_table
#include "migration/strategies/thread_strategy.hpp" // for select_migration
#include "migration/tickets.hpp"                    // for tickets_t, TICKE...
#include "system_info/system_info.hpp"              // for is_migratable
#include "utils/arithmetic.hpp"                     // for rnd
#include "utils/string.hpp"                         // for to_string
#include "utils/types.hpp"                          // for node_t, real_t
#include "utils/verbose.hpp"                        // for DEFAULT_LVL, lvl

namespace migration::thread {
	class random : public Istrategy {
	private:
		set<pid_t> migrated_pids_ = {};

		[[nodiscard]] auto get_migration(const pid_t tid) -> std::vector<migration_cell> {
			std::vector<migration_cell> migration_list;

			const auto src_cpu  = system_info::pinned_cpu_from_tid(tid);
			const auto src_node = system_info::node_from_cpu(src_cpu);
			const auto curr_pid = system_info::pid_from_tid(tid);

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "TID " << tid << ". Current node: " << src_node
				          << ", preferred: " << perf_table.preferred_node(tid) << '\n';
			}

			const auto dst_node  = std::cmp_greater(system_info::max_node(), 0) ?
			                           utils::arithmetic::rnd<node_t>(0, system_info::max_node()) :
			                           0;
			const auto cpus_node = system_info::num_of_cpus(dst_node);
			const auto dst_cpu = system_info::ith_cpu_from_node(dst_node, utils::arithmetic::rnd(size_t(0), cpus_node));

			tickets_t tickets = tickets_rm3d(tid, src_node, dst_node) + tickets_pref_node(tid, dst_node);

			simple_migration_cell mc(tid, curr_pid, dst_cpu, src_cpu, perf_table.performance(tid));

			// Free core: possible simple migration with a determined score
			if (system_info::is_cpu_free(dst_cpu)) {
				tickets += TICKETS_FREE_CORE;
				migration_list.emplace_back(std::initializer_list<simple_migration_cell>{ mc }, tickets);
				return migration_list;
			}

			// Not a free core: get its TIDs info so a possible interchange can be planned
			// We will take a random thread for interchange
			const auto & tids     = system_info::tids_from_cpu(dst_cpu);
			pid_t        swap_tid = 0;

			bool migr_found = false;

			for ([[maybe_unused]] const auto attempt : std::ranges::iota_view(size_t(), tids.size())) {
				auto adv_n   = utils::arithmetic::rnd(size_t(), tids.size());
				auto tids_it = tids.begin();
				std::ranges::advance(tids_it, adv_n, tids.end());
				swap_tid = *tids_it;

				// Check if it is viable to migrate the other thread
				if (system_info::is_migratable(swap_tid) && !migrated_pids_.contains(swap_tid)) {
					migr_found = true;
					break;
				}
			}

			if (migr_found) {
				const auto swap_pid  = system_info::pid_from_tid(swap_tid);
				const auto swap_perf = perf_table.performance(swap_tid);

				tickets += tickets_cpu(swap_tid, src_cpu);

				simple_migration_cell mc_2(swap_tid, swap_pid, src_cpu, dst_cpu, swap_perf);
				migration_list.emplace_back(std::initializer_list<simple_migration_cell>{ mc, mc_2 }, tickets);
			}

			return migration_list;
		}

		[[nodiscard]] auto get_migration_list(const umap<pid_t, real_t> & worst_tids) -> std::vector<migration_cell> {
			std::vector<migration_cell> migrations;

			for (const auto & [tid, perf] : worst_tids) {
				// check if tid has a destination already
				if (migrated_pids_.contains(tid)) { continue; }

				const auto migr = get_migration(tid);

				if (!migr.empty()) {
					const auto migration = select_migration(migr);
					const auto pids      = migration.tids_involved();

					migrated_pids_.insert(pids.begin(), pids.end());

					migrations.emplace_back(migration);
				}
			}

			return migrations;
		}

		[[nodiscard]] auto perform_migration_algorithm() -> std::vector<migration_cell> {
			umap<pid_t, real_t> threads_can_migrate;

			// Filter threads that can be migrated
			for (const auto & [tid, data] : perf_table) {
				const auto perf = perf_table.rel_performance(tid);

				if (system_info::is_migratable(tid)) { threads_can_migrate[tid] = perf; }
			}

			// Take random threads to migrate
			umap<pid_t, real_t> tid_perf_map;

			for ([[maybe_unused]] const auto i :
			     std::ranges::iota_view(size_t(), std::min(max_thread_migrations, perf_table.size()))) {
				auto map_it = perf_table.begin();

				// take a random thread that has not been selected yet
				auto random_idx = utils::arithmetic::rnd(size_t(), perf_table.size());

				std::ranges::advance(map_it, random_idx, perf_table.end());

				auto tid = map_it->first;

				// check if it was already selected
				while (tid_perf_map.contains(tid)) {
					map_it     = perf_table.begin();
					random_idx = utils::arithmetic::rnd(size_t(), perf_table.size());
					std::ranges::advance(map_it, random_idx, perf_table.end());
					tid = map_it->first;
				}

				tid_perf_map[tid] = perf_table.performance(tid);
			}

			// Print info of selected candidate threads to migrate
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				for (const auto & [tid, perf] : tid_perf_map) {
					std::cout << "TID " << tid << ", WITH RELATIVE PERF: " << utils::string::to_string(perf)
					          << " SELECTED FOR MIGRATION" << '\n';
				}
			}

			auto migrations = get_migration_list(tid_perf_map);

			if (migrations.empty()) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << "No candidate migrations." << '\n'; }
				return {};
			}

			// Print info of selected candidate threads to migrate
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "Selected migrations (" << migrations.size() << "):" << '\n';
				for (const auto & mig : migrations) {
					std::cout << mig << '\n';
				}
			}

			return migrations;
		}

	public:
		[[nodiscard]] auto migrate_to_nodes() const noexcept -> bool override {
			return false;
		}

		void migrate() override {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << '\n' << "RANDOM STRATEGY:" << '\n'; }

			real_t total_performance = perf_table.total_performance();

			// No performance obtained, so no suitable threads
			if (total_performance < performance::NEGLIGIBLE_PERFORMANCE) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Total performance: " << total_performance << '\n';
				}
				last_performance = performance::PERFORMANCE_INVALID_VALUE;
				return;
			}

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "Total performance: " << utils::string::to_string(total_performance)
				          << ". Last: " << utils::string::to_string(migration::thread::last_performance) << ". ";

				if (migration::thread::last_performance > 0) {
					const auto diff        = total_performance / migration::thread::last_performance;
					const auto improvement = 100 * (diff - 1);
					std::cout << "Improvement: " << utils::string::to_string(improvement) << "%";
				}
				std::cout << '\n';
			}

			last_performance = total_performance;

			// Remove last migrations (earlier migrations supposed successful at this point)
			last_migrations.clear();

			// Get the list of migrations to perform
			const auto migrations = perform_migration_algorithm();

			// Perform migrations
			for (const auto & mig : migrations) {
				// If the migration was successfully done...
				if (mig.migrate()) {
					++migration::thread::total_migrations;
					// Add to "undo" list
					last_migrations.emplace_back(mig);
				} else {
					undo_last_migrations();
					break;
				}
			}
		}
	};
} // namespace migration::thread

#endif /* end of include guard: THANOS_RANDOM_HPP */
