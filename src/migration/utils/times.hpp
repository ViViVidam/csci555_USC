/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_TIMES_HPP
#define THANOS_TIMES_HPP

#include <array>   // for array
#include <utility> // for cmp..

#include "utils/types.hpp" // for real_t

namespace migration {
	namespace thread {
		static constexpr auto SYS_TIME_NUM_VALUES = 3;
		static constexpr auto sys_time_values     = std::array<int, SYS_TIME_NUM_VALUES>{ 1, 2, 4 };

		extern real_t min_time_between_migrations;

		namespace details {
			extern int current_time_value;
		} // namespace details

		inline void time_go_up() {
			++details::current_time_value;
			if (std::cmp_greater_equal(details::current_time_value, SYS_TIME_NUM_VALUES)) {
				details::current_time_value = SYS_TIME_NUM_VALUES - 1;
			}
		}

		inline void time_go_down() {
			details::current_time_value--;
			if (std::cmp_less(details::current_time_value, 0)) { details::current_time_value = 0; }
		}

		[[nodiscard]] inline auto get_time_value() {
			return sys_time_values[details::current_time_value] * min_time_between_migrations;
		}
	} // namespace thread

	namespace memory {
		extern real_t min_time_between_migrations;
	} // namespace memory
} // namespace migration

#endif /* end of include guard: THANOS_TIMES_HPP */
