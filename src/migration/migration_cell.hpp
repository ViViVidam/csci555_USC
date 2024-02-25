/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MIGRATION_CELL_HPP
#define THANOS_MIGRATION_CELL_HPP

#include <features.h>  // for __glibc_likely
#include <iostream>    // for operator<<, basic_ostream::op...
#include <set>         // for set
#include <sys/types.h> // for pid_t, size_t
#include <utility>     // for move, swap
#include <vector>      // for vector

#include "migration/tickets.hpp"       // for operator<<, tickets_t
#include "performance/performance.hpp" // for PERFORMANCE_INVALID_VALUE
#include "system_info/system_info.hpp" // for node_from_cpu, set_tid_cpu
#include "utils/types.hpp"             // for real_t, cpu_t, node_t
#include "utils/verbose.hpp"           // for DEFAULT_LVL, lvl

namespace migration {

	class simple_migration_cell {
	private:
		pid_t tid_;  // Thread to migrate
		pid_t pid_;  // PID of the thread
		cpu_t dest_; // Destination (CPU/NODE)
		cpu_t prev_; // Previous (CPU/NODE)

		bool node_; // True if it is a migration at node level.

		real_t prev_perf_; // Previous performance

	public:
		simple_migration_cell() = delete;

		simple_migration_cell(const pid_t tid, const pid_t pid, const cpu_t dest, const cpu_t prev,
		                      const real_t prev_perf) :
		    tid_(tid), pid_(pid), dest_(dest), prev_(prev), node_(false), prev_perf_(prev_perf){};

		simple_migration_cell(const pid_t tid, const pid_t pid, const node_t dest, const node_t prev, const bool node,
		                      const real_t prev_perf) :
		    tid_(tid), pid_(pid), dest_(dest), prev_(prev), node_(node), prev_perf_(prev_perf){};

		[[nodiscard]] inline auto tid() const {
			return tid_;
		}

		[[nodiscard]] inline auto pid() const {
			return pid_;
		}

		[[nodiscard]] inline auto dest() const {
			return dest_;
		}

		[[nodiscard]] inline auto prev() const {
			return prev_;
		}

		inline void swap() {
			std::swap(prev_, dest_);
		}

		[[nodiscard]] inline auto is_node_migration() const {
			return node_;
		}

		[[nodiscard]] inline auto prev_perf() const {
			return prev_perf_;
		}

		[[nodiscard]] inline auto improvement(const real_t new_perf) const {
			return new_perf - prev_perf_;
		}

		[[nodiscard]] inline auto migrate() const -> bool {
			bool success = false;

			if (is_node_migration()) {
				success = system_info::set_tid_node(tid_, dest_);

				if (__glibc_likely(success)) {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Migrated thread " << tid_ << " to node " << dest_ << "." << '\n';
					}
				} else {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Failed to migrate thread " << tid_ << " to node " << dest_ << "." << '\n';
					}
				}
			} else {
				success = system_info::set_tid_cpu(tid_, dest_);

				const auto node = system_info::node_from_cpu(dest_);

				if (__glibc_likely(success)) {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Migrated thread " << tid_ << " to CPU " << dest_ << " (node " << node << ")."
						          << '\n';
					}
				} else {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Failed to migrate thread " << tid_ << " to CPU " << dest_ << " (node " << node
						          << ")." << '\n';
					}
				}
			}

			return success;
		}

		friend auto operator<<(std::ostream & os, const simple_migration_cell & mc) -> std::ostream & {
			if (mc.is_node_migration()) {
				os << "Thread migration cell."
				   << " TID " << mc.tid_ << " (PID " << mc.pid_ << ") to be migrated to NODE " << mc.dest()
				   << ". It was in NODE " << mc.prev() << ".";
			} else {
				const auto dst_node = system_info::node_from_cpu(mc.dest_);
				const auto src_node = system_info::node_from_cpu(mc.prev_);

				os << "Thread migration cell."
				   << " TID " << mc.tid_ << " (PID " << mc.pid_ << ") to be migrated to CPU " << mc.dest() << " (node "
				   << dst_node << "). It was in CPU " << mc.prev() << " (node " << src_node << ").";
			}

			return os;
		}
	};

	class migration_cell {
	private:
		std::vector<simple_migration_cell> migrations_;

		tickets_t tickets_ = {};

	public:
		explicit migration_cell(std::vector<simple_migration_cell> migrations) : migrations_(std::move(migrations)) {
		}

		migration_cell(std::vector<simple_migration_cell> migrations, const tickets_t tickets) :
		    migrations_(std::move(migrations)), tickets_(tickets) {
		}

		[[nodiscard]] inline auto begin() const {
			return migrations_.begin();
		}

		[[nodiscard]] inline auto end() const {
			return migrations_.end();
		}

		[[nodiscard]] inline auto tickets() const -> const auto & {
			return tickets_;
		}

		[[nodiscard]] inline auto migrations() const -> const auto & {
			return migrations_;
		}

		[[nodiscard]] inline auto migrations() -> auto & {
			return migrations_;
		}

		[[nodiscard]] inline auto migrate() const -> bool {
			bool success = true;
			for (const auto migr : migrations_) {
				success &= migr.migrate();
			}
			return success;
		}

		[[nodiscard]] inline auto is_interchange() const -> bool {
			return migrations_.size() == 2;
		}

		[[nodiscard]] inline auto tids_involved() const -> set<pid_t> {
			set<pid_t> tids;

			for (const auto migration : migrations_) {
				tids.insert(migration.tid());
			}

			return tids;
		}

		[[nodiscard]] inline auto balance(const std::vector<real_t> & new_perfs) const {
			real_t balance = 0;

			for (size_t i = 0; const auto migr : migrations_) {
				const real_t prev_perf =
				    migr.prev_perf() == performance::PERFORMANCE_INVALID_VALUE ? real_t() : migr.prev_perf();

				const real_t new_perf =
				    new_perfs[i] == performance::PERFORMANCE_INVALID_VALUE ? real_t() : new_perfs[i];
				balance += new_perf - prev_perf;

				++i;
			}

			return balance;
		}

		friend auto operator<<(std::ostream & os, const migration_cell & mig_cell) -> std::ostream & {
			if (mig_cell.is_interchange()) {
				os << "It is an interchange (" << utils::string::to_string(mig_cell.tickets().value())
				   << " tickets): " << '\n';
			} else {
				os << "It is a simple migration (" << utils::string::to_string(mig_cell.tickets().value())
				   << " tickets): " << '\n';
			}
			for (const auto & migration : mig_cell.migrations_) {
				os << "\t" << migration << '\n';
			}
			return os;
		}
	};
} // namespace migration

#endif /* end of include guard: THANOS_MIGRATION_CELL_HPP */
