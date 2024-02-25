/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEMORY_INFO_HPP
#define THANOS_MEMORY_INFO_HPP

#include <array>       // for array
#include <cerrno>      // for errno, EACCES, EBUSY
#include <cstring>     // for strerror
#include <exception>   // for exception
#include <iostream>    // for operator<<, basic_...
#include <map>         // for map, operator==
#include <ranges>      // for ranges::iota_view...
#include <stdexcept>   // for runtime_error
#include <string>      // for char_traits, opera...
#include <type_traits> // for __strip_reference_...
#include <utility>     // for pair, make_pair
#include <vector>      // for vector

#include <numa.h>   // for numa_move_pages
#include <numaif.h> // for MPOL_MF_MOVE
#include <unistd.h> // for size_t, pid_t, sys...

#include "memory/mem_region.hpp"                  // for mem_region, operat...
#include "memory/mem_region_numa_maps.hpp"        // for mem_region_numa_maps
#include "memory/thp.hpp"                         // for thp...
#include "memory/vmstat.hpp"                      // for vmstat_t, vmstat_t...
#include "system_info/memory/mem_region_maps.hpp" // for mem_region_maps
#include "system_info/system_info.hpp"            // for pid_is_lwp
#include "utils/string.hpp"                       // for to_string_hex
#include "utils/types.hpp"                        // for addr_t, node_t
#include "utils/verbose.hpp"                      // for lvl, DEFAULT_LVL

namespace memory_info {
	static const auto pagesize = sysconf(_SC_PAGESIZE);

	namespace details {
		static constexpr size_t DEFAULT_FAKE_THP_SIZE = 0; // Disabled by default

		extern vmstat_t<> vmstat;

		extern map<addr_t, mem_region> memory_regions;

		extern size_t fake_thp_size;

		extern map<addr_t, thp> fake_thp_regions;
	} // namespace details

	inline auto fake_thp_enabled() {
		return std::cmp_not_equal(details::fake_thp_size, 0);
	}

	inline auto fake_thp_size() {
		return details::fake_thp_size;
	}

	inline auto move_page(const addr_t addr, const pid_t pid, const int node) -> bool {
		std::array<void *, 1> pages = { reinterpret_cast<void *>(addr) };

		int status = 0;

		const auto ret = numa_move_pages(pid, 1, pages.data(), &node, &status, MPOL_MF_MOVE);

		if (std::cmp_less(ret, 0)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error migrating page address " << utils::string::to_string_hex(addr) << ": "
				          << strerror(errno) << '\n';
			}

			return false;
		}

		return true;
	}

	// Move additional pages to simulate prefetching
	inline auto move_pages(const addr_t addr, const size_t prefetch_size, const pid_t pid, const int node) -> bool {
		if (std::cmp_equal(prefetch_size, 0)) { return move_page(addr, pid, node); }

		size_t count = 1 + prefetch_size;

		std::vector<void *> pages(count, nullptr);
		std::vector<int>    statuses(count, 0);
		std::vector<int>    nodes(count, node);

		for (const auto i : std::ranges::iota_view(size_t(), count)) {
			pages[i] = reinterpret_cast<void *>(addr + i * pagesize);
		}

		const auto ret = numa_move_pages(pid, count, pages.data(), nodes.data(), statuses.data(), MPOL_MF_MOVE);

		if (std::cmp_less(ret, 0)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error migrating " << count << " pages starting from address "
				          << utils::string::to_string_hex(addr) << ": " << strerror(errno) << '\n';
			}

			return false;
		}

		return true;
	}

	inline auto move_pages(const std::vector<addr_t> & addresses, const pid_t pid, const std::vector<int> & nodes)
	    -> bool {
		size_t count = addresses.size();

		std::vector<void *> pages(count, nullptr);
		std::vector<int>    statuses(count, 0);

		for (size_t i = 0; const auto addr : addresses) {
			pages[i] = reinterpret_cast<void *>(addr);
			++i;
		}

		const auto ret = numa_move_pages(pid, count, pages.data(), nodes.data(), statuses.data(), MPOL_MF_MOVE);

		if (std::cmp_less(ret, 0)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error migrating " << count << " pages: " << strerror(errno) << '\n';
			}

			return false;
		}

		return true;
	}

	inline auto move_pages(const std::vector<addr_t> & addresses, const pid_t pid, const int node) {
		return move_pages(addresses, pid, std::vector<int>(addresses.size(), node));
	}

	// Currently, only normal ("small") pages are supported.
	// Hopefully, THP (Transparent Huge Pages) will be supported in the future
	[[nodiscard]] inline auto page_size([[maybe_unused]] const addr_t addr) -> size_t {
		return pagesize;
	}

	[[nodiscard]] inline auto page_from_addr(const addr_t addr) -> addr_t {
		return addr & ~(static_cast<addr_t>(page_size(addr) - 1));
	}

	[[nodiscard]] inline auto page_from_addr(const addr_t addr, const size_t size) -> addr_t {
		return addr & ~(static_cast<addr_t>(size - 1));
	}

	[[nodiscard]] inline auto get_page_current_node(const addr_t addr, const pid_t pid = 0) -> node_t {
		std::array<void *, 1> pages = { reinterpret_cast<void *>(addr) };

		int status = 0;

		// If "nodes" parameter is NULL, in "status" we obtain the node where the page is.
		const auto ret = numa_move_pages(pid, 1, pages.data(), nullptr, &status, 0);

		if (verbose::print_with_lvl(verbose::LVL_MAX) && std::cmp_not_equal(ret, 0)) {
			std::cerr << "Error " << errno << " getting location of page " << pages[0] << ": " << strerror(errno)
			          << '\n';
		}

		return status;
	}

	[[nodiscard]] inline auto get_pages_current_node(std::vector<addr_t> & pages, const pid_t pid = 0)
	    -> std::vector<node_t> {
		std::vector<int> status(pages.size(), -1);

		// If "nodes" parameter is NULL, in "status" we obtain the nodes where the pages are.
		const auto ret =
		    numa_move_pages(pid, pages.size(), reinterpret_cast<void **>(pages.data()), nullptr, status.data(), 0);

		if (verbose::print_with_lvl(verbose::LVL_MAX) && std::cmp_not_equal(ret, 0)) {
			std::cerr << "Error " << errno << " getting location of several pages: " << strerror(errno) << '\n';
		}

		return status;
	}

	static auto update_vmstat() -> bool {
		return details::vmstat.update();
	}

	[[nodiscard]] static auto region_from_address(const addr_t addr)
	    -> std::optional<std::reference_wrapper<const mem_region>> {
		// Try to find the memory region whose address is greater or equal
		auto it = details::memory_regions.lower_bound(addr);

		// If there is no match, .lower_bound() will return .end()
		if (it == details::memory_regions.end()) { return std::nullopt; }

		// If address matches we have been lucky
		if (it->second.address() == addr) { return { it->second }; }

		// We found a greater address, so try with the previous region

		// If it == .begin() but address does not match, we had bad luck
		if (it == details::memory_regions.begin()) { return std::nullopt; }

		// Go to the previous registered memory region
		--it;
		if (it->second.begin() < addr && addr < it->second.end()) { return { it->second }; }

		// No luck at all, return an empty optional...
		return std::nullopt;
	}

	[[nodiscard]] inline auto node_from_address(const addr_t addr) -> node_t {
		const auto page_addr = page_from_addr(addr);
		const auto page_node = get_page_current_node(page_addr);
		return page_node;
	}

	[[nodiscard]] inline auto nodes_from_adresses(const std::vector<addr_t> & addresses, const pid_t pid)
	    -> std::vector<node_t> {
		const auto count = addresses.size();

		std::vector<void *> pages(count, nullptr);
		for (const auto i : std::ranges::iota_view(size_t(), count)) {
			pages[i] = reinterpret_cast<void *>(addresses[i]);
		}

		std::vector<int> statuses(count, 0);

		int ret = numa_move_pages(pid, count, pages.data(), nullptr, statuses.data(), 0);

		if (ret < 0) { std::cerr << "Error retrieving nodes from page addresses: " << strerror(-ret) << '\n'; }

		std::vector<node_t> nodes(statuses);

		return nodes;
	}

	[[nodiscard]] auto is_huge_page(const addr_t addr) -> bool;

	[[nodiscard]] auto is_huge_page(const addr_t addr, const pid_t pid) -> bool;

	[[nodiscard]] static auto fake_thp_from_address(const addr_t addr)
	    -> std::optional<std::reference_wrapper<const thp>> {
		// Try to find the memory region whose address is greater or equal
		auto it = details::fake_thp_regions.lower_bound(addr);

		// If there is no match, .lower_bound() will return .end()
		if (it == details::fake_thp_regions.end()) { return std::nullopt; }

		// If address matches we have been lucky
		if (it->second.start() == addr) { return it->second; }

		// We found a greater address, so try with the previous region

		// If it == .begin() but address does not match, we had bad luck
		if (it == details::fake_thp_regions.begin()) { return std::nullopt; }

		// Go to the previous registered memory region
		--it;
		if (it->second.start() < addr && addr < it->second.end()) { return it->second; }

		// No luck at all, return an empty optional...
		return std::nullopt;
	}

	static void update_fake_thps() {
		if (!fake_thp_enabled()) { return; }

		details::fake_thp_regions.clear();

		for (const auto & [addr, region] : details::memory_regions) {
			auto start = region.address();

			while (std::cmp_less(start, region.end())) {
				const auto end = std::min(region.end(), start + pagesize * details::fake_thp_size);
				details::fake_thp_regions.emplace(start, thp(start, end));
				start = end;
			}
		}
	}

	static void update_memory_regions(pid_t pid) {
		umap<addr_t, mem_region_maps>      regions_maps;
		umap<addr_t, mem_region_numa_maps> regions_numa_maps;

		const auto maps_filename = "/proc/" + std::to_string(pid) + "/maps";

		std::ifstream maps(maps_filename);

		if (!maps.good()) {
			const auto error = "Cannot open file " + maps_filename + ": " + strerror(errno);
			throw std::runtime_error(error);
		}

		size_t i = 0;

		while (maps.good()) {
			std::string line;

			std::getline(maps, line);

			mem_region_maps region(line, i, pid);

			regions_maps.insert(std::make_pair(region.begin(), region));

			++i;
		}

		const auto numa_maps_filename = "/proc/" + std::to_string(pid) + "/numa_maps";

		std::ifstream numa_maps(numa_maps_filename);

		if (!numa_maps.good()) {
			const auto error = "Cannot open file " + numa_maps_filename + ": " + strerror(errno);
			throw std::runtime_error(error);
		}

		i = 0;

		while (numa_maps.good()) {
			std::string line;

			std::getline(numa_maps, line);

			mem_region_numa_maps region(line, i, pid);

			regions_numa_maps.insert(std::make_pair(region.address(), region));

			++i;
		}

		for (auto & [address, region_numa_maps] : regions_numa_maps) {
			const auto & region_maps = regions_maps.find(address);

			if (region_maps == regions_maps.end()) { continue; }

			details::memory_regions.insert(std::make_pair(address, mem_region(region_maps->second, region_numa_maps)));
		}
	}

	template<template<typename...> typename Iterable>
	static void update_memory_regions(const Iterable<pid_t> & pids) {
		details::memory_regions.clear();

		update_vmstat();

		for (const auto & pid : pids) {
			// If the PID corresponds to a LWP (Light Weight Process)...
			if (system_info::pid_is_lwp(pid)) {
				// Its memory footprint will be the same as its parent's, so no need to scan its memory /proc files
				continue;
			}
			try {
				update_memory_regions(pid);
			} catch (const std::exception & e) {
				if (verbose::print_with_lvl(verbose::LVL1)) { std::cerr << e.what() << '\n'; }
			} catch (...) {
				if (verbose::print_with_lvl(verbose::LVL1)) {
					std::cerr << "Could not update memory information of PID " << pid << '\n';
				}
			}
		}
	}

	[[nodiscard]] inline auto n_thps_all_regions() {
		return details::fake_thp_regions.size();
	}

	[[nodiscard]] inline auto n_pages_all_regions() {
		const auto n_pages = std::accumulate(details::memory_regions.begin(), details::memory_regions.end(), size_t(),
		                                     [&](const auto & acc, const auto & el) {
			                                     const auto & [addr, region] = el;
			                                     return acc + region.bytes() / pagesize;
		                                     });
		return n_pages;
	}

	[[nodiscard]] static auto contains(const addr_t addr) -> bool {
		// Try to find the memory region whose address is greater or equal
		auto it = details::memory_regions.lower_bound(addr);

		// If there is no match, .lower_bound() will return .end()
		if (it == details::memory_regions.end()) { return false; }

		// If address matches we have been lucky
		if (it->second.address() == addr) { return true; }

		// We found a greater address, so try with the previous region

		// If it == .begin() but address does not match, we had bad luck
		if (it == details::memory_regions.begin()) { return false; }

		// Go to the previous registered memory region
		--it;
		if (it->second.begin() < addr && addr < it->second.end()) { return true; }

		// No luck at all...
		return false;
	}

	template<template<typename...> typename Iterable>
	static void print_memory(const Iterable<pid_t> & pids, std::ostream & os = std::cout) {
		update_memory_regions(pids);

		os << "NUMA hit: " << details::vmstat.get_value(vmstat_t<>::numa_hit) << '\n';
		os << "NUMA miss: " << details::vmstat.get_value(vmstat_t<>::numa_miss) << '\n';
		os << "NUMA foreign: " << details::vmstat.get_value(vmstat_t<>::numa_foreign) << '\n';
		os << "NUMA interleave " << details::vmstat.get_value(vmstat_t<>::numa_interleave) << '\n';
		os << "NUMA local: " << details::vmstat.get_value(vmstat_t<>::numa_local) << '\n';
		os << "NUMA other: " << details::vmstat.get_value(vmstat_t<>::numa_other) << '\n';
		os << "NUMA pages migrated: " << details::vmstat.get_value(vmstat_t<>::numa_pages_migrated) << '\n';

		os << "Memory regions information for PIDs: ";
		for (const auto & pid : pids) {
			os << pid << ' ';
		}
		os << '\n';

		for (const auto & [address, region] : details::memory_regions) {
			os << region << '\n';
		}
	}
} // namespace memory_info


#endif /* end of include guard: THANOS_MEMORY_INFO_HPP */
