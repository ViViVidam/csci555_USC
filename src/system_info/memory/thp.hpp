/*
* ----------------------------------------------------------------------------
* "THE BEER-WARE LICENSE" (Revision 42):
* <r.laso@usc.es> wrote this file. As long as you retain this notice you
* can do whatever you want with this stuff. If we meet some day, and you think
* this stuff is worth it, you can buy me a beer in return Ruben Laso
* ----------------------------------------------------------------------------
*/

#ifndef THANOS_THP_HPP
#define THANOS_THP_HPP

#include <utils/types.hpp>

class thp {
private:
	addr_t start_; // start address of the THP
	addr_t end_;   // end of the THP

public:
	thp(const addr_t start, const addr_t end) : start_(start), end_(end) {
	}

	[[nodiscard]] inline auto start() const {
		return start_;
	}

	[[nodiscard]] inline auto end() const {
		return end_;
	}

	// size of the THP (in number of "normal" small pages)
	[[nodiscard]] inline auto n_pages() const {
		static const auto pagesize = sysconf(_SC_PAGESIZE);
		return (end_ - start_) / pagesize;
	}

	[[nodiscard]] inline auto to_pages() const -> std::vector<addr_t> {
		static const auto   pagesize = sysconf(_SC_PAGESIZE);
		std::vector<addr_t> pages;
		pages.reserve(n_pages());

		for (addr_t addr = start_; addr < end_; addr += pagesize) {
			pages.emplace_back(addr);
		}

		return pages;
	}
};

#endif /* end of include guard: THANOS_THP_HPP */
