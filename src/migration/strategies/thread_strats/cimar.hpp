/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_CIMAR_HPP
#define THANOS_CIMAR_HPP

#include <algorithm>   // for min, sort
#include <iostream>    // for operator<<, basi...
#include <set>         // for set, operator==
#include <string>      // for operator<<, char...
#include <sys/types.h> // for pid_t
#include <utility>     // for pair, tuple_elem...
#include <vector>      // for vector

#include "migration/migration_cell.hpp"             // for migration_cell
#include "migration/migration_var.hpp"              // for perf_table, last...
#include "migration/performance/performance.hpp"    // for PERFORMANCE_INVA...
#include "migration/performance/tid_perf_table.hpp" // for tid_perf_table
#include "migration/strategies/thread_strategy.hpp" // for tickets_cpu, mut...
#include "migration/tickets.hpp"                    // for tickets_t, PERF_...
#include "system_info/system_info.hpp"              // for pid_from_tid
#include "utils/string.hpp"                         // for to_string
#include "utils/types.hpp"                          // for real_t
#include "utils/verbose.hpp"                        // for DEFAULT_LVL, lvl

namespace migration::thread {
	class cimar : public Istrategy {
	private:
		static constexpr bool EVOLVE_TICKETS = false;

		set<pid_t> migrated_pids_ = {};

		[[nodiscard]] auto get_migration(const pid_t tid) -> std::vector<migration_cell> {
			std::vector<migration_cell> migration_list;

			const auto src_cpu   = system_info::pinned_cpu_from_tid(tid);
			const auto src_node  = system_info::node_from_cpu(src_cpu);
			const auto curr_pid  = system_info::pid_from_tid(tid);
			const auto curr_perf = perf_table.performance(tid);

			const auto src_tickets = tickets_cpu(tid, src_cpu);

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "TID " << tid << ". Current node: " << src_node
				          << ", preferred: " << perf_table.preferred_node(tid) << '\n';
			}

			// Search potential core destinations from different memory nodes
			for (const auto dst_node : system_info::nodes_by_distance(src_node)) {
				// Skip the current node...
				if (std::cmp_equal(src_node, dst_node)) { continue; }

				for (const auto dst_cpu : system_info::cpus_from_node(dst_node)) {
					simple_migration_cell mc(tid, curr_pid, dst_cpu, src_cpu, curr_perf);

					const auto dst_tickets = tickets_cpu(tid, dst_cpu);

					// Free core: possible simple migration with a determined score
					if (system_info::is_cpu_free(dst_cpu) && dst_tickets.value() > src_tickets.value()) {
						migration_list.emplace_back(std::initializer_list<simple_migration_cell>{ mc }, dst_tickets);
						continue;
					}

					// Not a free core: get its TIDs info so a possible interchange can be planned
					// We will choose the TID which generates the highest number of tickets
					const auto swap_tid_tickets_optional = best_pid_for_swap_cpu(src_cpu, dst_cpu, migrated_pids_);

					if (!swap_tid_tickets_optional.has_value()) {
						// there is no TID to swap with, continue searching...
						continue;
					}

					const auto [swap_tid, swap_tickets] = swap_tid_tickets_optional.value();

					const auto current_tickets   = src_tickets + tickets_cpu(swap_tid, dst_cpu);
					const auto estimated_tickets = dst_tickets + swap_tickets;

					if (estimated_tickets > current_tickets) {
						const auto swap_pid  = system_info::pid_from_tid(swap_tid);
						const auto swap_perf = perf_table.performance(swap_tid);

						simple_migration_cell mc_2(swap_tid, swap_pid, src_cpu, dst_cpu, swap_perf);
						migration_list.emplace_back(std::initializer_list<simple_migration_cell>{ mc, mc_2 },
						                            estimated_tickets);
					}
				}
			}

			return migration_list;
		}

		[[nodiscard]] auto get_migration_list(const std::vector<std::pair<real_t, pid_t>> & worst_tids)
		    -> std::vector<migration_cell> {
			std::vector<migration_cell> migrations;

			for (const auto & [perf, tid] : worst_tids) {
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
			std::vector<std::pair<real_t, pid_t>> worst_tid_perf;

			// Select worst threads
			for (const auto & [tid, data] : perf_table) {
				const auto perf = perf_table.rel_performance(tid);

				if (system_info::is_migratable(tid) && perf_table.is_running(tid) && perf < PERF_THRESHOLD &&
				    perf != performance::PERFORMANCE_INVALID_VALUE) {
					worst_tid_perf.emplace_back(perf, tid);
				}
			}

			if (worst_tid_perf.empty()) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "NO SUITABLE THREADS, NO MIGRATIONS" << '\n';
				}
				return {};
			}

			// Get first the TIDs with lower relative performance
			std::sort(worst_tid_perf.begin(), worst_tid_perf.end(),
			          [](const std::pair<real_t, pid_t> & a, const std::pair<real_t, pid_t> & b) {
				          const auto & [perf_a, pid_a] = a;
				          const auto & [perf_b, pid_b] = b;

				          return perf_a < perf_b;
			          });

			// Crop the list of tids
			worst_tid_perf.resize(std::min(max_thread_migrations, worst_tid_perf.size()));

			// Print info of selected candidate threads to migrate
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				for (const auto & [per, tid] : worst_tid_perf) {
					std::cout << "TID " << tid << ", WITH RELATIVE PERF: " << utils::string::to_string(per)
					          << " SELECTED FOR MIGRATION" << '\n';
				}
			}

			auto migrations = get_migration_list(worst_tid_perf);

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
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << '\n' << "CIMAR STRATEGY:" << '\n'; }

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

			if constexpr (EVOLVE_TICKETS) { mutate_tickets(); }

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

#endif /* end of include guard: THANOS_CIMAR_HPP */
