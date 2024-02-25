/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEM_MIGRATION_CELL_HPP
#define THANOS_MEM_MIGRATION_CELL_HPP

#include <iostream>    // for operator<<, basic_ostream::op...
#include <string>      // for operator<<, char_traits
#include <sys/types.h> // for pid_t, size_t
#include <utility>     // for move
#include <vector>      // for vector

#include "system_info/memory_info.hpp" // for move_pages
#include "utils/string.hpp"            // for percentage, to_string_hex
#include "utils/types.hpp"             // for real_t, node_t, addr_t
#include "utils/verbose.hpp"           // for LVL4, lvl

namespace migration {

	class mem_migration_cell {
	private:
		static constexpr size_t DEFAULT_PREFETCH = 0;

		std::vector<addr_t> addr_; // Address of the page to migrate
		pid_t               pid_;  // PID of the process owning the memory page
		node_t              src_;  // Previous node
		node_t              dst_;  // Destination node

		std::vector<real_t> ratios_; // Accesses ratios...

	public:
		mem_migration_cell() = delete;

		mem_migration_cell(std::vector<addr_t> addr, const pid_t pid, const node_t src, const node_t dst,
		                   std::vector<real_t> ratios) :
		    addr_(std::move(addr)), pid_(pid), src_(src), dst_(dst), ratios_(std::move(ratios)){};

		[[nodiscard]] inline auto addr() const -> auto & {
			return addr_;
		}

		[[nodiscard]] inline auto size() const {
			return addr_.size();
		}

		[[nodiscard]] inline auto pid() const -> pid_t {
			return pid_;
		}

		[[nodiscard]] inline auto dst() const -> node_t {
			return dst_;
		}

		[[nodiscard]] inline auto migrate() const -> bool {
			if (addr_.empty()) {
				std::cerr << "Memory migration cell is empty, skipping..." << '\n';
				return false;
			}

			bool success = memory_info::move_pages(addr_, pid_, dst_);

			if (success) {
				if (verbose::print_with_lvl(verbose::LVL4)) {
					if (std::cmp_greater(addr_.size(), 1)) {
						std::cout << "Migrated " << addr_.size() << " memory pages starting from ";
					} else {
						std::cout << "Migrated memory page ";
					}
					std::cout << utils::string::to_string_hex(addr_[0]) << " (PID " << pid_ << ") to node " << dst_
					          << " (" << utils::string::percentage(ratios_[dst_]) << "% of the accesses) from node "
					          << src_ << '\n';
				}
			} else {
				if (verbose::print_with_lvl(verbose::LVL4)) {
					if (std::cmp_greater(addr_.size(), 1)) {
						std::cout << "Failed to migrate " << addr_.size() << " memory pages starting from ";
					} else {
						std::cout << "Failed to migrate memory page ";
					}
					std::cout << utils::string::to_string_hex(addr_[0]) << " (PID " << pid_ << ") to node " << dst_
					          << " (" << utils::string::percentage(ratios_[dst_]) << "% of the accesses) from node "
					          << src_ << '\n';
				}
			}

			return success;
		}

		friend auto operator<<(std::ostream & os, const mem_migration_cell & mc) -> std::ostream & {
			if (mc.addr_.empty()) {
				os << "Empty migration cell.";
			} else {
				os << "Memory page migration cell."
				   << " Address " << utils::string::to_string_hex(mc.addr_[0]) << " (PID " << mc.pid_
				   << ") to be migrated to NODE " << mc.dst_ << " (" << utils::string::percentage(mc.ratios_[mc.dst_])
				   << "% of the accesses). It was in NODE " << mc.src_ << ".";
			}
			return os;
		}
	};
} // namespace migration

#endif /* end of include guard: THANOS_MEM_MIGRATION_CELL_HPP */