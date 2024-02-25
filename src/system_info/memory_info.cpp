/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include "memory_info.hpp"

#include <concepts>

#include "types.hpp" // for addr_t

namespace memory_info {
	namespace details {
		vmstat_t<> vmstat;

		map<addr_t, mem_region> memory_regions;

		size_t fake_thp_size = DEFAULT_FAKE_THP_SIZE;

		map<addr_t, thp> fake_thp_regions;
	} // namespace details

	namespace {
		[[nodiscard]] inline auto is_bigendian() {
			static const auto _is_bigendian = std::endian::native == std::endian::big;
			return _is_bigendian;
		}

		[[nodiscard]] inline auto is_bit_set(const std::integral auto & from, const std::integral auto & bit) {
			return std::cmp_not_equal(0, from & ((uint64_t) 1 << bit) >> bit);
		}

		// Return the PFN from the given 64-bit /proc/<PID>/pagemap entry
		[[nodiscard]] inline auto pfn_from_pagemap_entry(const std::integral auto & entry) {
			return entry & 0x7FFFFFFFFFFFFF;
		}

		// Returns the PFN (Physical Frame Number) of a given page
		[[nodiscard]] inline auto get_pfn(const addr_t addr, const pid_t pid) -> std::optional<addr_t> {
			static constexpr auto ENTRY_SIZE = 8;

			const auto path = "/proc/" + std::to_string(pid) + "/pagemap";

			std::unique_ptr<std::FILE, decltype(&std::fclose)> file_ptr(std::fopen(path.c_str(), "rb"), &std::fclose);
			if (file_ptr == nullptr) { throw std::runtime_error("Could not open file " + path); }

			//Shifting by virt-addr-offset number of bytes
			//and multiplying by the size of an address (the size of an entry in pagemap file)
			const auto file_offset = static_cast<long>(addr) / getpagesize() * ENTRY_SIZE;
			const auto status      = fseek(file_ptr.get(), file_offset, SEEK_SET);
			if (std::cmp_not_equal(status, 0)) {
				throw std::runtime_error("Could not perform seek() over file: " + std::string(path));
			}

			uint64_t read_val = 0;

			std::array<unsigned char, ENTRY_SIZE> c_buf{};

			if (is_bigendian()) {
				for (auto & c : c_buf) {
					c = getc(file_ptr.get());
					if (std::cmp_equal(c, EOF)) { return {}; }
				}
			} else {
				for (int i = 0; i < ENTRY_SIZE; i++) {
					auto c = getc(file_ptr.get());
					if (std::cmp_equal(c, EOF)) { return {}; }
					c_buf.at(ENTRY_SIZE - i - 1) = c;
				}
			}
			for (const auto & i : c_buf) {
				read_val = (read_val << 8) + i;
			}

			static constexpr auto PAGE_PRESENT = 63;
			static constexpr auto PAGE_SWAPPED = 62;
			// If "swapped" or "not present", there won't be any PFN
			if (is_bit_set(read_val, PAGE_SWAPPED) || is_bit_set(read_val, PAGE_PRESENT)) { return {}; }
			return pfn_from_pagemap_entry(read_val);
		}

		// Returns true if the given PFN corresponds to a THP (Transparent Huge Page)
		[[nodiscard]] inline auto is_huge_kpageflags(const uint64_t pfn) -> bool {
			static constexpr std::string_view path("/proc/kpageflags");

			static constexpr auto ENTRY_SIZE = 8;

			std::unique_ptr<std::FILE, decltype(&std::fclose)> file_ptr(std::fopen(path.data(), "rb"), &std::fclose);
			if (file_ptr == nullptr) { throw std::runtime_error("Could not open file " + std::string(path)); }

			const auto file_offset = static_cast<long>(pfn) * ENTRY_SIZE;
			const auto status      = fseek(file_ptr.get(), file_offset, SEEK_SET);
			if (std::cmp_not_equal(status, 0)) {
				throw std::runtime_error("Could not perform seek() over file: " + std::string(path));
			}

			uint64_t read_val = 0;

			std::array<unsigned char, ENTRY_SIZE> c_buf{};

			if (is_bigendian()) {
				for (auto & c : c_buf) {
					c = getc(file_ptr.get());
					if (std::cmp_equal(c, EOF)) { return {}; }
				}
			} else {
				for (int i = 0; i < ENTRY_SIZE; i++) {
					auto c = getc(file_ptr.get());
					if (std::cmp_equal(c, EOF)) { return {}; }
					c_buf.at(ENTRY_SIZE - i - 1) = c;
				}
			}
			for (const auto & i : c_buf) {
				read_val = (read_val << 8) + i;
			}
			for (const auto & i : c_buf) {
				read_val = (read_val << sizeof(char)) + static_cast<unsigned char>(i);
			}

			static constexpr auto HUGE_FLAG = 17;
			return is_bit_set(read_val, HUGE_FLAG);
		}
	} // namespace

	[[nodiscard]] auto is_huge_page(const addr_t addr) -> bool {
		const auto region_optional = region_from_address(addr);

		if (!region_optional.has_value()) { return false; }

		const auto & region = region_optional.value().get();

		const auto pfn_optional = get_pfn(addr, region.pid());

		if (pfn_optional.has_value()) { return is_huge_kpageflags(pfn_optional.value()); }

		return false;
	}

	[[nodiscard]] auto is_huge_page(const addr_t addr, const pid_t pid) -> bool {
		const auto pfn_optional = get_pfn(addr, pid);

		if (pfn_optional.has_value()) { return is_huge_kpageflags(pfn_optional.value()); }

		return false;
	}
} // namespace memory_info
