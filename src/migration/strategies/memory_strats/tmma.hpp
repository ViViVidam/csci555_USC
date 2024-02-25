/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_TMMA_HPP
#define THANOS_TMMA_HPP

#include <algorithm>   // for min
#include <cstddef>     // for size_t
#include <iostream>    // for operator<<, basi...
#include <numa.h>      // for numa_max_node
#include <numeric>     // for accumulate
#include <string>      // for operator<<, char...
#include <type_traits> // for add_const<>::type
#include <utility>     // for cmp...
#include <vector>      // for vector

#include "migration/mem_migration_cell.hpp"         // for mem_migration_cell
#include "migration/migration_var.hpp"              // for perf_table, last...
#include "migration/performance/mempages_table.hpp" // for mempages_table
#include "migration/performance/tid_perf_table.hpp" // for tid_perf_table
#include "migration/strategies/memory_strategy.hpp" // for Istrategy...
#include "utils/string.hpp"                         // for percentage, to_s...
#include "utils/types.hpp"                          // for addr_t, real_t
#include "utils/verbose.hpp"                        // for lvl, DEFAULT_LVL

namespace migration::memory {
	[[nodiscard]] inline auto min_ratio_to_mig() noexcept -> real_t {
		static constexpr real_t two_thirds = 2.0 / 3.0;
		return std::min(two_thirds, real_t(2) / static_cast<real_t>(numa_max_node() + 1));
	}

	const real_t min_ratio_mig = min_ratio_to_mig();

	// Threshold Memory pages Migration Algorithm
	class tmma : public migration::memory::Istrategy {
	private:
		[[nodiscard]] static auto perform_migration_algorithm_all_pages() -> std::vector<mem_migration_cell> {
			uset<addr_t> pages_to_migrate;

			std::vector<mem_migration_cell> migrations;

			// Preallocate some memory to save time later
			migrations.reserve(perf_table.size());

			for (auto & [mem_page, info] : perf_table) {
				// If the page was already considered for migration...
				if (pages_to_migrate.contains(mem_page)) {
					// Keep going with the next page
					continue;
				}

				if (!info.enough_info()) { continue; }

				const auto curr_node = info.last_node();
				const auto pref_node = info.preferred_node();

				const auto ratios    = info.ratios();
				const auto max_ratio = ratios[pref_node];

				if (std::cmp_not_equal(pref_node, curr_node) && max_ratio > min_ratio_mig) {
					pages_to_migrate.insert(mem_page);

					const auto pid = info.last_pid();

					std::vector<addr_t> pages{ mem_page };

					const auto pages_to_prefetch = prefetch_candidates(mem_page, pref_node);

					pages.insert(pages.end(), pages_to_prefetch.begin(), pages_to_prefetch.end());

					pages_to_migrate.insert(pages.begin(), pages.end());

					mem_migration_cell migration(pages, pid, curr_node, pref_node, ratios);

					migrations.emplace_back(pages, pid, curr_node, pref_node, ratios);

					info.clear();
				}
			}

			if (verbose::print_with_lvl(verbose::LVL4)) {
				std::cout << "Pages to migrate (w/o prefetching): " << migrations.size() << " ("
				          << utils::string::percentage(migrations.size(), perf_table.size()) << "%)" << '\n';
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

			std::vector<addr_ratio_mig_t> candidates;

			// Preallocate some memory to save time later
			candidates.reserve(perf_table.size());

			for (auto & [mem_page, info] : perf_table) {
				// If the page was already considered for migration...
				if (pages_to_migrate.contains(mem_page)) {
					// Keep going with the next page
					continue;
				}

				if (!info.enough_info()) { continue; }

				const auto curr_node = info.last_node();
				const auto pref_node = info.preferred_node();

				const auto ratios    = info.ratios();
				const auto max_ratio = ratios[pref_node];

				if (std::cmp_not_equal(pref_node, curr_node) && max_ratio > min_ratio_mig) {
					pages_to_migrate.insert(mem_page);

					const auto pid = info.last_pid();

					std::vector<addr_t> pages{ mem_page };

					const auto pages_to_prefetch = prefetch_candidates(mem_page, pref_node);

					pages.insert(pages.end(), pages_to_prefetch.begin(), pages_to_prefetch.end());

					pages_to_migrate.insert(pages.begin(), pages.end());

					mem_migration_cell migration(pages, pid, curr_node, pref_node, ratios);

					candidates.emplace_back(mem_page, max_ratio, migration);

					info.clear();
				}
			}

			if (verbose::print_with_lvl(verbose::LVL4)) {
				std::cout << "Pages with ratio > threshold: " << candidates.size() << " ("
				          << utils::string::percentage(candidates.size(), perf_table.size()) << "%)" << '\n';
			}

			const auto n_migrations = std::min(n_pages, candidates.size());

			auto migrations = select_best_migrations(candidates, n_migrations);

			if (verbose::print_with_lvl(verbose::LVL4)) {
				std::cout << "Pages to migrate (w/o prefetching): " << migrations.size() << " ("
				          << utils::string::percentage(migrations.size(), perf_table.size()) << "%)" << '\n';
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
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cout << '\n' << "TMMA STRATEGY:" << '\n'; }

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

#endif /* end of include guard: THANOS_TMMA_HPP */