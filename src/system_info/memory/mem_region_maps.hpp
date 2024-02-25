/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEM_REGION_MAPS_HPP
#define THANOS_MEM_REGION_MAPS_HPP

#include <cerrno>      // for errno
#include <climits>     // for PATH_MAX
#include <cstdint>     // for uint32_t
#include <cstdio>      // for sscanf, size_t, sprintf
#include <cstring>     // for strcmp, strerror, size_t
#include <fstream>     // for operator<<, basic_ostream, ostringstream
#include <stdexcept>   // for runtime_error
#include <string>      // for string, getline, operator<<
#include <sys/types.h> // for pid_t

#include "utils/types.hpp" // for addr_t

class mem_region_maps {
private:
	static constexpr const char SHARED  = 's';
	static constexpr const char PRIVATE = 'p'; // copy on write

	static constexpr const char READ    = 'r';
	static constexpr const char WRITE   = 'w';
	static constexpr const char EXECUTE = 'x';

	static constexpr const char NO_PERMISSION = '-';

	static constexpr const char * const STACK     = "[stack]";      // Main process stack
	static constexpr const char * const STACK_TID = "[heap:<tid>]"; // A thread's stack
	static constexpr const char * const VDSO      = "[vdso]";       // The virtual dynamically linked shared object
	static constexpr const char * const HEAP      = "[heap]";       // The process's heap

	size_t index_;
	pid_t  tid_;

	addr_t begin_{};
	addr_t end_{};

	size_t bytes_{};

	std::array<char, 5> flags_{ '\0' };

	uint32_t offset_{};

	uint32_t device_maj_{};
	uint32_t device_min_{};

	uint32_t inode_{};

	std::array<char, PATH_MAX> path_{ '\0' };

	inline void parse_line(const std::string & line) {
		std::sscanf(line.c_str(), "%lx-%lx %4c %x %x:%x %x %s", &begin_, &end_, flags_.data(), &offset_, &device_maj_,
		            &device_min_, &inode_, path_.data());
		flags_.back() = '\0';

		bytes_ = end_ - begin_;
	}

public:
	mem_region_maps(const pid_t pid, const size_t index) : index_(index), tid_(pid) {
		const auto filename = "/proc/" + std::to_string(pid) + "/maps";

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

	mem_region_maps(const std::string & line_info, const size_t & index, const pid_t & tid) : index_(index), tid_(tid) {
		parse_line(line_info);
	}

	[[nodiscard]] inline auto index() const {
		return index_;
	}

	[[nodiscard]] inline auto begin() const {
		return begin_;
	}

	[[nodiscard]] inline auto end() const {
		return end_;
	}

	[[nodiscard]] inline auto bytes() const {
		return bytes_;
	}

	[[nodiscard]] inline auto flags() const {
		return flags_.data();
	}

	[[nodiscard]] inline auto path() const {
		return path_;
	}

	[[nodiscard]] inline auto read() const -> bool {
		return flags_[0] == READ;
	}

	[[nodiscard]] inline auto write() const -> bool {
		return flags_[1] == WRITE;
	}

	[[nodiscard]] inline auto execute() const -> bool {
		return flags_[2] == EXECUTE;
	}

	[[nodiscard]] inline auto is_private() const -> bool {
		return flags_[4] == PRIVATE;
	}

	[[nodiscard]] inline auto is_shared() const -> bool {
		return flags_[4] == SHARED;
	}

	[[nodiscard]] inline auto heap() -> bool {
		bool is_heap = false;

		// First check if it is main process's stack
		is_heap = strcmp(path_.data(), HEAP) == 0;

		if (!is_heap) {
			std::array<char, PATH_MAX> heap_tid{ '\0' };
			sprintf(heap_tid.data(), "[heap:%ul]", tid_);
			is_heap = strcmp(path_.data(), heap_tid.data()) == 0;
		}

		return is_heap;
	}

	[[nodiscard]] inline auto stack() const -> bool {
		return strcmp(path_.data(), STACK) == 0;
	}

	[[nodiscard]] inline auto vdso() const -> bool {
		return strcmp(path_.data(), VDSO) == 0;
	}

	inline friend auto operator<<(std::ostream & os, const mem_region_maps & m) -> std::ostream & {
		os << std::hex << m.begin_ << '-' << m.end_ << ' ' << m.flags_.data() << ' ' << m.offset_ << ' '
		   << m.device_maj_ << ':' << m.device_min_ << ' ' << std::dec << m.inode_ << "\t\t\t" << m.path_.data();

		return os;
	}
};

#endif /* end of include guard: THANOS_MEM_REGION_MAPS_HPP */
