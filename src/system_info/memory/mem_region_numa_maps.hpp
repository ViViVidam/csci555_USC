/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEM_REGION_NUMA_MAPS_HPP
#define THANOS_MEM_REGION_NUMA_MAPS_HPP

#include <cerrno>      // for errno
#include <climits>     // for PATH_MAX
#include <cstdio>      // for sscanf
#include <cstring>     // for size_t, strerror
#include <istream>     // for operator<<, basic_ostream
#include <ranges>      // for ranges::iota_view...
#include <stdexcept>   // for runtime_error
#include <string>      // for string, operator<<, getline
#include <sys/types.h> // for pid_t
#include <vector>      // for allocator, vector

#include "system_info/system_info.hpp" // for num_of_nodes
#include "utils/types.hpp"             // for addr_t

class mem_region_numa_maps {
protected:
	static constexpr size_t ADDRESS_POS = 0;
	static constexpr size_t POLICY_POS  = 1;

	static constexpr const char * STACK_STR = "stack";
	static constexpr const char * HEAP_STR  = "heap";
	static constexpr const char * HUGE_STR  = "huge";

private:
	size_t index_; // Row number within /proc/<pid>/numa_maps
	pid_t  pid_;   // PID of the process owning the memory page

	// The first field of each line shows the starting address of the memory
	// range.  This field allows a correlation with the contents of the
	// /proc/<pid>/maps file, which contains the end address of the range
	// and other information, such as the access permissions and sharing.
	addr_t address_{};

	// The second field shows the memory policy currently in effect for the
	// memory range.  Note that the effective policy is not necessarily the
	// policy installed by the process for that memory range.  Specifically,
	// if the process installed a "default" policy for that range, the
	// effective policy for that range will be the process policy, which may
	// or may not be "default".
	std::string policy_ = {};

	// N<node>=<nr_pages>
	// The number of pages allocated on <node>. <nr_pages> includes
	// only pages currently mapped by the process. Page migration
	// and memory reclaim may have temporarily unmapped pages
	// associated with this memory range.  These pages may show up
	// again only after the process has attempted to reference them.
	// If the memory range represents a shared memory area or file
	// mapping, other processes may currently have additional pages
	// mapped in a corresponding memory range.
	std::vector<size_t> pages_per_node_;

	// The file backing the memory range.  If the file is mapped as
	// private, write accesses may have generated COW (Copy-On-Write)
	// pages in this memory range.  These pages are displayed as
	// anonymous pages.
	std::string file_ = {};

	bool heap_  = false; // Memory range is used for the heap.
	bool stack_ = false; // Memory range is used for the stack.
	bool huge_  = false; // Huge memory range. The page counts shown are huge pages and not regular sized pages.

	size_t anon_      = 0; // The number of anonymous page in the range.
	size_t dirty_     = 0; // Number of dirty pages.
	size_t mapped_    = 0; // Total number of mapped pages, if different from dirty and anon pages.
	size_t mapmax_    = 0; // Maximum mapcount (number of processes mapping a single page) encountered during the scan.
	                       // This may be used as an indicator of the degree of sharing
	                       // occurring in a given memory range.
	size_t swapcache_ = 0; // Number of pages that have an associated entry on a swap device.
	size_t active_    = 0; // The number of pages on the active list. This field is shown only if different from the
	                       // number of pages in this range. This means that some inactive pages exist
	                       // in the memory range that may be removed from memory by the swapper soon.
	size_t writeback_ = 0; // Number of pages that are currently being written out to disk.

	size_t kernelpagesize_kB_ = 4; // Size of each memory page

protected:
	auto parse_parameter(const std::string & parameter) -> bool {
		const char * string = parameter.c_str();

		// Try parsing N<node>=<nr_pages>
		int node  = 0;
		int value = 0;

		if (sscanf(string, "N%d=%d", &node, &value) > 0) {
			pages_per_node_[node] = value;
			return true;
		}

		// Try parsing file=<filename>
		std::array<char, PATH_MAX> filename{ '\0' };
		if (sscanf(string, "file=%s", filename.data()) > 0) {
			file_ = std::string(filename.data());
			return true;
		}

		// Try parsing "heap", "stack" or "huge"
		if (std::cmp_not_equal(parameter.find(STACK_STR), std::string::npos)) {
			stack_ = true;
			return true;
		}

		if (std::cmp_not_equal(parameter.find(HEAP_STR), std::string::npos)) {
			heap_ = true;
			return true;
		}

		if (std::cmp_not_equal(parameter.find(HUGE_STR), std::string::npos)) {
			huge_ = true;
			return true;
		}

		// Try parsing anon=<pages>
		if (std::cmp_greater(sscanf(string, "anon=%d", &value), 0)) {
			anon_ = value;
			return true;
		}

		// Try parsing dirty=<pages>
		if (std::cmp_greater(sscanf(string, "dirty=%d", &value), 0)) {
			dirty_ = value;
			return true;
		}

		// Try parsing mapped=<pages>
		if (std::cmp_greater(sscanf(string, "mapped=%d", &value), 0)) {
			mapped_ = value;
			return true;
		}

		// Try parsing mapmax=<count>
		if (std::cmp_greater(sscanf(string, "mapmax=%d", &value), 0)) {
			mapmax_ = value;
			return true;
		}

		// Try parsing swapcache=<count>
		if (std::cmp_greater(sscanf(string, "swapcache=%d", &value), 0)) {
			swapcache_ = value;
			return true;
		}

		// Try parsing writeback=<pages>
		if (std::cmp_greater(sscanf(string, "writeback=%d", &value), 0)) {
			writeback_ = value;
			return true;
		}

		// Try parsing kernelpagesize_Kb=<pagesize>
		if (std::cmp_greater(sscanf(string, "kernelpagesize_kB=%d", &value), 0)) {
			kernelpagesize_kB_ = value;
			return true;
		}

		return false;
	}

	void parse_line(const std::string & line_) {
		std::string line(line_);

		constexpr char SEP = ' ';

		size_t pos = 0;

		std::string token;

		token = line.substr(0, pos = line.find(SEP));
		sscanf(token.c_str(), "%lx", &address_);
		line.erase(0, pos + 1); // +1 because of separator char

		policy_ = line.substr(0, pos = line.find(SEP));
		line.erase(0, pos + 1); // +1 because of separator char

		while ((pos = line.find(SEP)) != std::string::npos) {
			token = line.substr(0, pos);

			parse_parameter(token);

			line.erase(0, pos + 1);
		}
	}

public:
	mem_region_numa_maps(const pid_t pid, const size_t index) :
	    index_(index), pid_(pid), pages_per_node_(system_info::max_node() + 1) {
		const auto filename = "/proc/" + std::to_string(pid) + "/numa_maps";

		std::ifstream file(filename);

		if (!file.good()) {
			const auto error = "Cannot open file " + filename + ": " + strerror(errno);
			throw std::runtime_error(error);
		}

		bool index_found = false;

		size_t i = 0;

		while (file.good()) {
			std::string line;

			std::getline(file, line);

			++i;

			if (std::cmp_equal(i, index)) {
				index_found = true;
				parse_line(line);
				break;
			}
		}

		if (!index_found) {
			const auto error = "Cannot found line number " + std::to_string(index);
			throw std::runtime_error(error);
		}
	}

	mem_region_numa_maps(const std::string & line_info, const size_t & index, const pid_t & tid) :
	    index_(index), pid_(tid), pages_per_node_(system_info::num_of_nodes()) {
		parse_line(line_info);
	}

	[[nodiscard]] inline auto index() const {
		return index_;
	}

	[[nodiscard]] inline auto pid() const {
		return pid_;
	}

	[[nodiscard]] inline auto address() const {
		return address_;
	}

	[[nodiscard]] inline auto policy() const -> const auto & {
		return policy_;
	}

	[[nodiscard]] inline auto pages_per_node() const -> const auto & {
		return pages_per_node_;
	}

	[[nodiscard]] inline auto file() const -> const auto & {
		return file_;
	}

	[[nodiscard]] inline auto heap() const {
		return heap_;
	}

	[[nodiscard]] inline auto stack() const {
		return stack_;
	}

	[[nodiscard]] inline auto huge() const {
		return huge_;
	}

	[[nodiscard]] inline auto anon() const {
		return anon_;
	}

	[[nodiscard]] inline auto dirty() const {
		return dirty_;
	}

	[[nodiscard]] inline auto mapped() const {
		return mapped_;
	}

	[[nodiscard]] inline auto mapmax() const {
		return mapmax_;
	}

	[[nodiscard]] inline auto swapcache() const {
		return swapcache_;
	}

	[[nodiscard]] inline auto active() const {
		return active_;
	}

	[[nodiscard]] inline auto writeback() const {
		return writeback_;
	}

	[[nodiscard]] inline auto kernelpagesize_kB() const {
		return kernelpagesize_kB_;
	}

	inline friend auto operator<<(std::ostream & os, const mem_region_numa_maps & m) -> std::ostream & {
		os << std::hex << m.address_ << std::dec << ' ' << m.policy_ << ' ';

		os << "Pages per node: ";
		for (const auto n : system_info::nodes()) {
			os << m.pages_per_node_[n] << (std::cmp_not_equal(n, m.pages_per_node_.size() - 1) ? ',' : ' ');
		}

		os << m.file_ << ' ';

		if (m.stack_) { os << STACK_STR << ' '; }
		if (m.heap_) { os << HEAP_STR << ' '; }
		if (m.huge_) { os << HUGE_STR << ' '; }

		/* clang-format off */
		os << "anon="              << m.anon_      << ' ';
		os << "dirty="             << m.dirty_     << ' ';
		os << "mapped="            << m.mapped_    << ' ';
		os << "mapmax="            << m.mapmax_    << ' ';
		os << "swapcache="         << m.swapcache_ << ' ';
		os << "active="            << m.active_    << ' ';
		os << "writeback="         << m.writeback_ << ' ';
		os << "kernelpagesize_kB=" << m.kernelpagesize_kB_;
		/* clang-format on */

		return os;
	}
};

#endif /* end of include guard: THANOS_MEM_REGION_NUMA_MAPS_HPP */
