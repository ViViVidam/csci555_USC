/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEMORY_STRATEGY_HPP
#define THANOS_MEMORY_STRATEGY_HPP

#include <algorithm>     // for sort
#include <map>           // for map, operator==
#include <ranges>        // for ranges::iota_view...
#include <sys/types.h>   // for pid_t, size_t
#include <tuple>         // for tuple
#include <type_traits>   // for add_const<>::type
#include <unordered_map> // for operator==, _Nod...
#include <utility>       // for pair
#include <utility>       // for cmp
#include <vector>        // for vector

#include "migration/mem_migration_cell.hpp"         // for mem_migration_cell
#include "migration/migration_var.hpp"              // for memory_prefetch_...
#include "migration/performance/mempages_table.hpp" // for mempages_table, row
#include "system_info/memory_info.hpp"              // for move_pages, page...
#include "utils/types.hpp"                          // for addr_t, node_t

namespace migration::memory {
	class Istrategy {
	public:
		Istrategy()                        = default;
		Istrategy(const Istrategy & strat) = default;
		Istrategy(Istrategy && strat)      = default;

		virtual ~Istrategy() = default;

		auto operator=(const Istrategy &) -> Istrategy & = default;
		auto operator=(Istrategy &&) -> Istrategy &      = default;

		// tuple = [address, max_ratio, migration_cell]
		using addr_ratio_mig_t = std::tuple<addr_t, real_t, mem_migration_cell>;

		[[nodiscard]] static auto prefetch_candidates(const addr_t initial, const node_t dst) -> std::vector<addr_t> {
			std::vector<addr_t> pages;
			pages.reserve(memory_prefetch_size);

			const auto pagesize   = memory_info::page_size(initial);
			auto       region_end = initial + (memory_prefetch_size + 1) * pagesize;

			if (memory_info::fake_thp_enabled()) {
				const auto thp_opt = memory_info::fake_thp_from_address(initial);
				if (thp_opt.has_value()) {
					const auto & thp = thp_opt.value().get();

					region_end = thp.end();

					return thp.to_pages();
				}
			}

			for (const addr_t i : std::ranges::iota_view(size_t(1), memory_prefetch_size + 1)) {
				const addr_t candidate = initial + i * pagesize;

				if (std::cmp_greater_equal(candidate, region_end)) {
					// We reached the end of memory region... Stop prefetching
					break;
				}

				const auto & page_it = perf_table.find(candidate);

				// If we have no information about the page
				if (page_it == perf_table.end()) {
					// Assume everything is gonna be alright, so prefetch it...
					pages.emplace_back(candidate);
					continue;
				}

				const auto & info = page_it->second;

				const auto pref_node = info.preferred_node();

				// Check if the preferred nodes are the same
				if (std::cmp_equal(pref_node, dst)) {
					pages.emplace_back(candidate);
				} else {
					// Stop prefetching
					break;
				}
			}

			return pages;
		}

		[[nodiscard]] static auto prefetch_candidates_smallpages(const addr_t initial, const node_t dst)
		    -> std::vector<addr_t> {
			std::vector<addr_t> pages;
			// Preallocate memory to save time
			pages.reserve(memory_prefetch_size);

			for (const addr_t i : std::ranges::iota_view(size_t(1), memory_prefetch_size + 1)) {
				const addr_t candidate = initial + i * memory_info::pagesize;

				const auto & page_it = perf_table.find(candidate);

				// If we have no information about the page
				if (page_it == perf_table.end()) {
					// Assume everything is gonna be alright, so prefetch it...
					pages.emplace_back(candidate);
					continue;
				}

				const auto & info = page_it->second;

				const auto pref_node = info.preferred_node();

				// Check if the preferred nodes are the same
				if (std::cmp_equal(pref_node, dst)) {
					pages.emplace_back(candidate);
				} else {
					break;
				}
			}

			return pages;
		}

		[[nodiscard]] static auto select_best_migrations(std::vector<addr_ratio_mig_t> & candidates,
		                                                 const size_t n_migrations) -> std::vector<mem_migration_cell> {
			// If we have more candidates than the max. pages to migrate, we have to take the most promising ones...
			if (std::cmp_greater(candidates.size(), n_migrations)) {
				// Sort candidates by ratio. First has a higher max. ratio.
				std::sort(candidates.begin(), candidates.end(),
				          [&](const addr_ratio_mig_t & a, const addr_ratio_mig_t & b) {
					          const auto & [addr_a, ratio_a, mig_cell_a] = a;
					          const auto & [addr_b, ratio_b, mig_cell_b] = b;

					          return ratio_a > ratio_b;
				          });
			}

			std::vector<mem_migration_cell> migrations;
			migrations.reserve(n_migrations);

			for (const auto & candidate : candidates) {
				const auto & [addr, ratio, mig_cell] = candidate;
				migrations.emplace_back(mig_cell);
			}

			return migrations;
		}

		[[nodiscard]] static auto gather_and_perform_migrations(const std::vector<mem_migration_cell> & migrations)
		    -> size_t {
			using tuple_t = std::pair<std::vector<addr_t>, std::vector<node_t>>;
			umap<pid_t, tuple_t> migrations_tuples;

			for (const auto & migration : migrations) {
				const auto pid = migration.pid();

				auto & [addresses, nodes] = migrations_tuples[pid];

				for (const auto addr : migration.addr()) {
					addresses.emplace_back(addr);
					nodes.emplace_back(migration.dst());
				}
			}

			size_t migrated_pages = 0;

			for (const auto & [pid, addr_node_map] : migrations_tuples) {
				const auto & [addresses, nodes] = addr_node_map;

				if (memory_info::move_pages(addresses, pid, nodes)) {
					migrated_pages += addresses.size();
					migration::memory::total_migrations += addresses.size();
				}
			}

			return migrated_pages;
		}

		virtual void migrate() = 0;
	};
} // namespace migration::memory

#endif /* end of include guard: THANOS_MEMORY_STRATEGY_HPP */
