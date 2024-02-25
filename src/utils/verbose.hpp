/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_VERBOSE_HPP
#define THANOS_VERBOSE_HPP

#include <concepts>
#include <utility>

namespace verbose {
	enum level_t {
		NO_VERBOSE = 0,
		LVL1,
		LVL2,
		LVL3,
		LVL4,
		LVL_MAX,
	};

	static constexpr auto DEFAULT_LVL = LVL3;

	extern level_t lvl;

	inline auto print_with_lvl(const level_t min_lvl) -> bool {
		return lvl >= min_lvl;
	}

	template<typename T>
	concept to_level_t = requires(T new_lvl) {
		                     { new_lvl } -> std::common_reference_with<level_t>;
	                     };

	inline auto change(const to_level_t auto new_lvl) {
		switch (new_lvl) {
			case NO_VERBOSE:
				return lvl = NO_VERBOSE;
			case LVL1:
				return lvl = LVL1;
			case LVL2:
				return lvl = LVL2;
			case LVL3:
				return lvl = LVL3;
			case LVL4:
				return lvl = LVL4;
			case LVL_MAX:
				return lvl = LVL_MAX;
			default:
				if (new_lvl > LVL_MAX) {
					return lvl = LVL_MAX;
				} else if (new_lvl < NO_VERBOSE) {
					return lvl = NO_VERBOSE;
				} else {
					return lvl = DEFAULT_LVL;
				}
		}
	}
} // namespace verbose

#endif /* end of include guard: THANOS_VERBOSE_HPP */
