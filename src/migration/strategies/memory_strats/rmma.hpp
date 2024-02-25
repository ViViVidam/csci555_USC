/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_RMMA_HPP
#define THANOS_RMMA_HPP

#include <cstddef>     // for size_t
#include <iostream>    // for operator<<, basi...
#include <iterator>    // for advance
#include <numeric>     // for accumulate
#include <string>      // for operator<<, char...
#include <type_traits> // for add_const<>::type
#include <utility>     // for cmp...
#include <vector>      // for vector

#include "migration/mem_migration_cell.hpp"         // for mem_migration_cell
#include "migration/migration_var.hpp"              // for perf_table, last...
#include "migration/performance/mempages_table.hpp" // for mempages_table
#include "migration/performance/tid_perf_table.hpp" // for tid_perf_table
#include "migration/strategies/memory_strategy.hpp" // for Istrategy
#include "system_info/system_info.hpp"              // for num_of_nodes
#include "utils/arithmetic.hpp"                     // for rnd
#include "utils/string.hpp"                         // for to_string, perce...
#include "utils/types.hpp"                          // for real_t, node_t
#include "utils/verbose.hpp"                        // for DEFAULT_LVL, lvl

namespace migration::memory {
	// Latency Memory pages Migration Algorithm
	class rmma : public migration::memory::Istrategy {
	private:
		[[nodiscard]] static auto perform_migration_algorithm_all_pages() -> std::vector<mem_migration_cell> {
			std::vector<mem_migration_cell> migrations;

			// Preallocate some memory to save time later
			migrations.reserve(perf_table.size());

			for (auto & [mem_page, info] : perf_table) {
				const auto   dst_node = utils::arithmetic::rnd<node_t>(0, system_info::max_node());
				const auto   src_node = info.last_node();
				const auto   pid      = info.last_pid();
				const auto & ratios   = info.ratios();

				if (std::cmp_not_equal(dst_node, src_node)) {
					migrations.emplace_back(std::vector<addr_t>{ mem_page }, pid, src_node, dst_node, ratios);
					info.clear();
				}
			}

			return migrations;
		}

		[[nodiscard]] static auto perform_migration_algorithm_n_pages(const size_t n_pages)
		    -> std::vector<mem_migration_cell> {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "Max. pages to migrate: " << n_pages << " / " << perf_table.size() << " ("
				          << utils::string::percentage(portion_memory_migrations) << "%)" << '\n';
			}

			uset<addr_t> pages_to_migrate;

			std::vector<mem_migration_cell> migrations;

			// Preallocate some memory to save time later
			migrations.reserve(perf_table.size());

			size_t i = n_pages;

			while (i-- > 0) {
				// Go to a random page
				auto it = perf_table.begin();

				const auto r = utils::arithmetic::rnd(size_t(), perf_table.size());

				std::ranges::advance(it, r, perf_table.end());

				const auto & [mem_page, info] = *it;

				// Move it to a random destination (if we did not before)
				// If the page was already considered for migration...
				if (pages_to_migrate.contains(mem_page)) {
					// Keep going with the next page
					continue;
				}

				const auto   dst_node = utils::arithmetic::rnd({}, static_cast<node_t>(system_info::num_of_nodes()));
				const auto   src_node = info.last_node();
				const auto   pid      = info.last_pid();
				const auto & ratios   = info.ratios();

				if (std::cmp_not_equal(dst_node, src_node)) {
					migrations.emplace_back(std::vector<addr_t>{ mem_page }, pid, src_node, dst_node, ratios);
				}
			}

			return migrations;
		}

		[[nodiscard]] static auto perform_migration_algorithm() -> std::vector<mem_migration_cell> {
			if (std::cmp_equal(system_info::num_of_nodes(), 1)) { return {}; }

			const auto max_pages_to_migrate =
			    static_cast<size_t>(portion_memory_migrations * static_cast<real_t>(perf_table.size()));

			if (std::cmp_equal(max_pages_to_migrate, 0)) { return {}; }

			if (std::cmp_equal(max_pages_to_migrate, perf_table.size())) {
				return perform_migration_algorithm_all_pages();
			}

			return perform_migration_algorithm_n_pages(max_pages_to_migrate);
		}

	public:
		void migrate() override {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << '\n' << "RMMA STRATEGY:" << '\n'; }

			real_t total_performance = migration::thread::perf_table.total_performance();

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

			// Get the list of migrations to perform
			const auto migrations = perform_migration_algorithm();

			// Perform migrations
			size_t performed_migrations = gather_and_perform_migrations(migrations);

			if (verbose::print_with_lvl(verbose::LVL2)) {
				const auto total_candidates = std::accumulate(migrations.begin(), migrations.end(), 0,
				                                              [](size_t accum, const mem_migration_cell & m) -> size_t {
					                                              return accum + m.addr().size();
				                                              });
				std::cout << "Successfully migrated " << performed_migrations << " memory pages of " << total_candidates
				          << " candidates (" << utils::string::percentage(performed_migrations, total_candidates, 0)
				          << "% of pages). Including prefetching." << '\n';
			}
		}
	};
} // namespace migration::memory

#endif /* end of include guard: THANOS_RMMA_HPP */
