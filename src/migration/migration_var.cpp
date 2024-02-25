/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include "migration_var.hpp"

#include "migration_cell.hpp"               // for migration_cell
#include "performance/performance.hpp"      // for PERFORMANCE_INVALID_VALUE
#include "strategies/memory_mig_strats.hpp" // for DEFAULT_STRATEGY, strate...
#include "strategies/thread_mig_strats.hpp" // for DEFAULT_STRATEGY, strate...
#include "types.hpp"                        // for hres_clock, real_t, time...

namespace migration {
	namespace memory {
		strategy_t strategy = DEFAULT_STRATEGY;

		time_point last_mig_time = hres_clock::now();

		size_t total_migrations        = 0;
		size_t total_migrations_undone = 0;

		size_t num_mem_samples_it = 0;

		performance::mempages_table perf_table;

		real_t portion_memory_migrations = DEFAULT_PORTION_MEM_MIGS;
		size_t memory_prefetch_size      = DEFAULT_MEMORY_PREFETCH;
	} // namespace memory

	namespace thread {
		strategy_t strategy = DEFAULT_STRATEGY;

		time_point last_mig_time = hres_clock::now();

		size_t total_migrations        = 0;
		size_t total_migrations_undone = 0;

		size_t num_mem_samples_it = 0;
		size_t num_req_samples_it = 0;
		size_t num_ins_samples_it = 0;

		performance::tid_perf_table perf_table;

		size_t max_thread_migrations = DEFAULT_MAX_MIGRATIONS;

		real_t last_performance = performance::PERFORMANCE_INVALID_VALUE;

		std::vector<migration_cell> last_migrations;
	} // namespace thread
} // namespace migration