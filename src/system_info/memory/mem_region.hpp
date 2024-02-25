/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEM_REGION_HPP
#define THANOS_MEM_REGION_HPP

#include <fstream>     // for operator<<, basic_ostream, ostream
#include <ranges>      // for ranges::iota_view...
#include <string>      // for operator<<, string
#include <sys/types.h> // for size_t, pid_t
#include <vector>      // for vector

#include "mem_region_maps.hpp"      // for mem_region_maps
#include "mem_region_numa_maps.hpp" // for mem_region_numa_maps, mem_region...
#include "utils/string.hpp"         // for to_string_hex

class mem_region : public mem_region_maps, public mem_region_numa_maps {
public:
	mem_region(const pid_t pid, const size_t maps_index, const size_t numa_maps_index) :
	    mem_region_maps(pid, maps_index), mem_region_numa_maps(pid, numa_maps_index) {
	}

	mem_region(const pid_t pid, const std::string & line_maps, const size_t index_maps,
	           const std::string & line_numa_maps, const size_t index_numa_maps) :
	    mem_region_maps(line_maps, index_maps, pid), mem_region_numa_maps(line_numa_maps, index_numa_maps, pid) {
	}

	mem_region(mem_region_maps & maps, mem_region_numa_maps & numa_maps) :
	    mem_region_maps(maps), mem_region_numa_maps(numa_maps) {
	}

	[[nodiscard]] inline auto index_maps() const {
		return mem_region_maps::index();
	}

	[[nodiscard]] inline auto index_numa_maps() const {
		return mem_region_numa_maps::index();
	}

	inline friend auto operator<<(std::ostream & os, const mem_region & m) -> std::ostream & {
		os << utils::string::to_string_hex(m.begin()) << '-' << utils::string::to_string_hex(m.end()) << std::dec << ' '
		   << m.bytes() << "B " << m.flags() << ' ' << "policy=" << m.policy() << ' ';
		os << "Pages per node: ";
		for (const auto n : system_info::nodes()) {
			os << m.pages_per_node()[n] << (std::cmp_not_equal(n, m.pages_per_node().size() - 1) ? ',' : ' ');
		}

		os << m.file() << ' ';

		if (m.mem_region_numa_maps::stack()) { os << STACK_STR << ' '; }
		if (m.mem_region_numa_maps::heap()) { os << HEAP_STR << ' '; }
		if (m.huge()) { os << HUGE_STR << ' '; }

		/* clang-format off */
		os << "anon="              << m.anon()      << ' ';
		os << "dirty="             << m.dirty()     << ' ';
		os << "mapped="            << m.mapped()    << ' ';
		os << "mapmax="            << m.mapmax()    << ' ';
		os << "swapcache="         << m.swapcache() << ' ';
		os << "active="            << m.active()    << ' ';
		os << "writeback="         << m.writeback() << ' ';
		os << "kernelpagesize_kB=" << m.kernelpagesize_kB();
		/* clang-format on */

		return os;
	}
};

#endif /* end of include guard: THANOS_MEM_REGION_HPP */