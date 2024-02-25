/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_TICKETS_HPP
#define THANOS_TICKETS_HPP

#include <cstdint> // for uint8_t

#include <iostream> // for istream, ostream

#include "utils/types.hpp" // for real_t

namespace migration {
	using tickets_val_t = real_t;

	static constexpr tickets_val_t DEFAULT_TICKETS_MEM_CELL_WORSE   = 1;
	static constexpr tickets_val_t DEFAULT_TICKETS_MEM_CELL_NO_DATA = 2;
	static constexpr tickets_val_t DEFAULT_TICKETS_MEM_CELL_BETTER  = 4;
	static constexpr tickets_val_t DEFAULT_TICKETS_FREE_CORE = 2; // Migrations to free cores are cheaper than swaps
	static constexpr tickets_val_t DEFAULT_TICKETS_PREF_NODE = 4;
	static constexpr tickets_val_t DEFAULT_TICKETS_THREAD_UNDER_PERF = 3;

	static constexpr real_t DEFAULT_PERF_THRESHOLD = 0.8;
	static constexpr real_t DEFAULT_UNDO_THRESHOLD = 0.9;

	enum tickets_mask : uint8_t {
		TICKETS_MEM_CELL_WORSE_MASK    = 1,
		TICKETS_MEM_CELL_NO_DATA_MASK  = 2,
		TICKETS_MEM_CELL_BETTER_MASK   = 4,
		TICKETS_FREE_CORE_MASK         = 8,
		TICKETS_PREF_NODE_MASK         = 16,
		TICKETS_THREAD_UNDER_PERF_MASK = 32
	};

	using tickets_mask_t = uint8_t;

	class tickets_t {
	private:
		tickets_val_t  value_ = {};
		tickets_mask_t mask_  = {};

	public:
		tickets_t() noexcept = default;

		explicit tickets_t(tickets_val_t value, tickets_mask_t mask) : value_(value), mask_(mask){};

		[[nodiscard]] inline auto value() const {
			return value_;
		}

		[[nodiscard]] inline auto mask() const {
			return mask_;
		}

		inline void value(tickets_val_t val) {
			value_ = val;
		}

		inline void mask(tickets_mask_t mask) {
			mask_ = mask;
		}

		inline void operator+=(const tickets_t & rhs) {
			value_ += rhs.value_;
			mask_ |= rhs.mask_;
		}

		inline void operator-=(const tickets_t & rhs) {
			value_ -= rhs.value_;
			mask_ ^= rhs.mask_;
		}

		[[nodiscard]] inline auto operator+(const tickets_t & rhs) const -> tickets_t {
			tickets_t tickets = *this;
			tickets += rhs;
			return tickets;
		}

		[[nodiscard]] inline auto operator-(const tickets_t & rhs) const -> tickets_t {
			tickets_t tickets = *this;
			tickets -= rhs;
			return tickets;
		}

		[[nodiscard]] inline auto operator<=>(const tickets_t & rhs) const {
			return value_ <=> rhs.value_;
		}

		inline friend auto operator<<(std::ostream & os, const tickets_t & tickets) -> std::ostream & {
			os << tickets.value_;
			return os;
		}

		inline friend auto operator>>(std::istream & is, tickets_t & tickets) -> std::istream & {
			is >> tickets.value_;
			return is;
		}
	};

	extern tickets_t TICKETS_MEM_CELL_WORSE;
	extern tickets_t TICKETS_MEM_CELL_NO_DATA;
	extern tickets_t TICKETS_MEM_CELL_BETTER;
	extern tickets_t TICKETS_FREE_CORE;
	extern tickets_t TICKETS_PREF_NODE;
	extern tickets_t TICKETS_UNDER_PERF;

	extern real_t PERF_THRESHOLD;
	extern real_t UNDO_THRESHOLD;

	static constexpr const char * FILE_TICKETS     = "tickets.opt";
	static constexpr const char * FILE_TICKETS_CSV = "tickets.csv";

	auto read_tickets_file(const char * filename_ = FILE_TICKETS) -> bool;

	auto write_tickets_file(const char * filename_ = FILE_TICKETS) -> bool;

	auto write_tickets_csv_header(const char * filename_ = FILE_TICKETS_CSV) -> bool;

	auto write_tickets_csv(const char * filename_ = FILE_TICKETS_CSV) -> bool;
} // namespace migration

#endif /* end of include guard: THANOS_TICKETS_HPP */
