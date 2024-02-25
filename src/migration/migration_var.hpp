/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MIGRATION_VAR_HPP
#define THANOS_MIGRATION_VAR_HPP

#include <cstddef> // for size_t
#include <vector>  // for vector

#include "migration/migration_cell.hpp"               // for migration_cell
#include "migration/strategies/memory_mig_strats.hpp" // for strategy_t
#include "migration/strategies/thread_mig_strats.hpp" // for strategy_t
#include "performance/mempages_table.hpp"             // for mempages_table
#include "performance/tid_perf_table.hpp"             // for tid_perf_table
#include "utils/types.hpp"                            // for real_t, time_p...

namespace migration {
	namespace memory {
		extern strategy_t strategy;

		extern time_point last_mig_time;

		extern size_t total_migrations;
		extern size_t total_migrations_undone;

		extern size_t num_mem_samples_it;

		extern performance::mempages_table perf_table;

		static constexpr real_t DEFAULT_PORTION_MEM_MIGS = 1.0;
		static constexpr size_t DEFAULT_MEMORY_PREFETCH  = 8;

		extern real_t portion_memory_migrations;
		extern size_t memory_prefetch_size;

	} // namespace memory

	namespace thread {
		extern strategy_t strategy;

		extern time_point last_mig_time;

		extern size_t total_migrations;
		extern size_t total_migrations_undone;

		extern size_t num_mem_samples_it;
		extern size_t num_req_samples_it;
		extern size_t num_ins_samples_it;

		extern performance::tid_perf_table perf_table;

		static constexpr size_t DEFAULT_MAX_MIGRATIONS = 5;

		extern size_t max_thread_migrations;

		extern real_t last_performance;

		extern std::vector<migration_cell> last_migrations;
	} // namespace thread
} // namespace migration

#endif /* end of include guard: THANOS_MIGRATION_VAR_HPP */