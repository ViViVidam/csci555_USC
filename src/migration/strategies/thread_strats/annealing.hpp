/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_ANNEALING_HPP
#define THANOS_ANNEALING_HPP

#include <cmath>       // for isnormal
#include <cstddef>     // for size_t
#include <iostream>    // for operator<<, basi...
#include <map>         // for operator==, _Rb_...
#include <string>      // for operator<<
#include <type_traits> // for add_const<>::type
#include <utility>     // for cmp...
#include <vector>      // for vector

#include "migration/migration_cell.hpp"             // for migration_cell
#include "migration/migration_var.hpp"              // for last_performance
#include "migration/performance/performance.hpp"    // for NEGLIGIBLE_PERFO...
#include "migration/performance/tid_perf_table.hpp" // for tid_perf_table
#include "migration/strategies/thread_strategy.hpp" // for thread_t, schedu...
#include "system_info/system_info.hpp"              // for pinned_cpu_from_tid
#include "utils/arithmetic.hpp"                     // for rnd
#include "utils/string.hpp"                         // for to_string
#include "utils/types.hpp"                          // for real_t
#include "utils/verbose.hpp"                        // for lvl, DEFAULT_LVL

namespace migration::thread {
	class annealing_node : public Istrategy {
	private:
		// Mapping improving >10% are worth, no matter what
		static constexpr real_t IMPROVEMENT_TO_BE_WORTH = 0.10;

		// Mappings improving <1% are not worth, no matter what
		static constexpr real_t MIN_IMPR_TO_BE_WORTH = 0.01;

		template<class T>
		[[nodiscard]] static inline auto is_better(const T current, const T best) -> bool {
			return current > best;
		}

		template<class T>
		[[nodiscard]] static inline auto probability(const T current, const T best, const real_t temperature)
		    -> real_t {
			const real_t probability = (1.0 - (best - current) / best) * temperature;
			if (verbose::print_with_lvl(verbose::LVL_MAX)) { std::cout << "Probability: " << probability << '\n'; }
			return std::isnormal(probability) ? probability : 0;
		}

		[[nodiscard]] static auto optimal_schedule(const mapping::schedule_t & init_solution) -> mapping::schedule_t {
			static constexpr size_t MAX_ITER             = 100;
			static constexpr size_t MAX_ITER_WO_IMPR     = 20;
			static constexpr size_t MAX_MUTATIONS_PER_IT = 1;
			static constexpr real_t INITIAL_TEMP         = 0.10;
			static constexpr real_t SCALE_TEMP           = 0.97;

			mapping::schedule_t curr_solution(init_solution);
			mapping::schedule_t best_solution(init_solution);

			const auto init_tickets = init_solution.expected_tickets();
			auto       curr_tickets = init_tickets;
			auto       best_tickets = init_tickets;

			size_t iter             = 0;
			size_t iter_without_imp = 0;

			real_t temperature = INITIAL_TEMP;

			if (verbose::print_with_lvl(verbose::LVL4)) {
				std::cout << "Iter " << iter << ". Initial tickets: " << utils::string::to_string(init_tickets) << '\n';
			}

			for (iter = 0; iter < MAX_ITER && iter_without_imp < MAX_ITER_WO_IMPR; ++iter) {
				// Get a candidate (neighbour) solution
				const auto cand_solution = curr_solution.neighbour(MAX_MUTATIONS_PER_IT);
				// Compute its expected tickets
				const auto cand_tickets  = cand_solution.expected_tickets();

				if (verbose::print_with_lvl(verbose::LVL4)) {
					const auto improvement_to_curr = (cand_tickets - curr_tickets) / curr_tickets;
					const auto improvement_to_best = (cand_tickets - best_tickets) / best_tickets;

					std::cout << "Iter " << iter << ". Candidate tickets: " << utils::string::to_string(cand_tickets)
					          << ". Current: " << utils::string::to_string(curr_tickets)
					          << " (Impr: " << utils::string::to_string(improvement_to_curr) << "%)"
					          << ". Best: " << utils::string::to_string(best_tickets)
					          << ". (Impr: " << utils::string::to_string(improvement_to_best) << "%)" << '\n';
				}

				// Accept the candidate solution
				if (is_better(cand_tickets, curr_tickets)) {
					if (verbose::print_with_lvl(verbose::LVL4)) {
						std::cout << "Iter " << iter
						          << ". New target tickets: " << utils::string::to_string(cand_tickets)
						          << ". Old: " << utils::string::to_string(curr_tickets) << '\n';
					}

					curr_solution = cand_solution;
					curr_tickets  = cand_tickets;

					// Check if the candidate is better than the current best
					if (is_better(cand_tickets, best_tickets)) {
						if (verbose::print_with_lvl(verbose::LVL4)) {
							std::cout
							    << "Iter " << iter
							    << ". New best solution found with tickets: " << utils::string::to_string(cand_tickets)
							    << ". Old: " << utils::string::to_string(best_tickets) << '\n';
						}

						best_solution = cand_solution;
						best_tickets  = cand_tickets;

						iter_without_imp = 0;
					}
				} else if (utils::arithmetic::rnd(0.0, 1.0) < probability(cand_tickets, curr_tickets, temperature)) {
					++iter_without_imp;

					// Accept the candidate even if it is worse than the current one (within a decreasing probability)
					if (verbose::print_with_lvl(verbose::LVL4)) {
						std::cout
						    << "Iter " << iter
						    << ". Accepted worst solution. New tickets: " << utils::string::to_string(cand_tickets)
						    << ". Old: " << utils::string::to_string(curr_tickets) << '\n';
					}

					curr_solution = cand_solution;
					curr_tickets  = cand_tickets;
				} else {
					++iter_without_imp;
				}

				// Update the temperature
				temperature = INITIAL_TEMP * SCALE_TEMP;

				if (verbose::print_with_lvl(verbose::LVL_MAX)) {
					std::cout << "Iter " << iter << ". New temperature: " << temperature << '\n';
				}
			}

			const auto improvement = (best_tickets - init_tickets) / init_tickets * 100;

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				const auto diff = best_tickets - init_tickets;
				std::cout << "Iter " << iter << ". Initial solution tickets: " << utils::string::to_string(init_tickets)
				          << ". New: " << utils::string::to_string(best_tickets)
				          << ". Potential improvement: " << utils::string::to_string(diff) << " ("
				          << utils::string::to_string(improvement) << "%)" << '\n';
			}

			return best_solution;
		}

		[[nodiscard]] static auto get_migration_list(const std::vector<mapping::thread_t> & threads)
		    -> std::vector<migration_cell> {
			// This strategy looks for optimizing the number of local accesses
			// Uses annealing for searching the optimal thread location

			if (std::cmp_equal(system_info::num_of_nodes(), 1)) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Just one memory... Nothing to do." << '\n';
				}
				return {};
			}

			mapping::schedule_t init_solution(threads);

			auto best_solution = optimal_schedule(init_solution);

			const auto init_tickets = init_solution.expected_tickets();
			const auto best_tickets = best_solution.expected_tickets();

			const auto improvement = (best_tickets - init_tickets) / init_tickets * real_t(100);

			auto migrations = best_solution.migrations();

			// New mapping is much better than the old one -> Seems worth, let's go!
			if (improvement > IMPROVEMENT_TO_BE_WORTH) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Huge improvement (" << utils::string::to_string(improvement) << "%). Seems worth..."
					          << '\n';
				}
				return migrations;
			}

			const auto n_threads_mig = n_threads_involved(migrations);

			// New mapping is better than the old one and it requires moving little threads -> Seems worth, let's go!
			if (improvement > MIN_IMPR_TO_BE_WORTH && improvement > static_cast<real_t>(n_threads_mig)) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "#Migrations (" << n_threads_mig << ") <= Improvement ("
					          << utils::string::to_string(improvement) << "%). Seems worth..." << '\n';
				}
				return migrations;
			}

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "Improvement (" << utils::string::to_string(improvement) << "%) < #Migrations ("
				          << n_threads_mig << "). Seems not worth..." << '\n';
			}

			return {};
		}

		[[nodiscard]] static auto perform_migration_algorithm() -> std::vector<migration_cell> {
			std::vector<mapping::thread_t> threads;

			for (const auto & [tid, data] : perf_table) {
				const auto cpu  = system_info::pinned_cpu_from_tid(tid);
				const auto node = system_info::node_from_cpu(cpu);

				threads.emplace_back(tid, data.performance(), node);
			}

			if (threads.empty()) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cerr << "No threads to migrate..." << '\n'; }
				return {};
			}

			auto migrations = get_migration_list(threads);

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
			return true;
		}

		void migrate() override {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << '\n' << "Annealing (at node level) STRATEGY:" << '\n';
			}

			real_t total_performance = perf_table.total_performance();

			// No performance obtained, so no suitable threads
			if (total_performance < performance::NEGLIGIBLE_PERFORMANCE) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Total performance: " << utils::string::to_string(total_performance) << '\n';
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

#endif /* end of include guard: THANOS_ANNEALING_HPP */
