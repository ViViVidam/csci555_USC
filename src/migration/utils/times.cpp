/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include "times.hpp"

#include "types.hpp" // for real_t

namespace migration {
	namespace thread {
		real_t min_time_between_migrations = 1;

		namespace details {
			int current_time_value = 0;
		} // namespace details

	} // namespace thread

	namespace memory {
		real_t min_time_between_migrations = 1;
	} // namespace memory
} // namespace migration