/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_TIME_HPP
#define THANOS_TIME_HPP

#include <chrono>      // for operator-, duration
#include <ctime>       // for strftime, time, localtime
#include <string>      // for allocator, string
#include <string_view> // for string_view

#include "types.hpp" // for time_point, hres_clock, real_t

namespace utils::time {
	static constexpr auto SECS_TO_USECS = 1000000;

	static constexpr auto DEFAULT_DATE_FORMAT = "%d-%m-%Y_%H:%M:%S";

	const time_point start_exec = hres_clock::now();

	template<typename T = real_t>
	inline auto time_until(const time_point & begin, const time_point & end) -> T {
		return std::chrono::duration<T>(end - begin).count();
	}

	template<typename T = real_t>
	inline auto time_until_now(const time_point & begin) -> T {
		return time_until<T>(begin, hres_clock::now());
	}

	template<typename T = real_t>
	inline auto time_until_now() -> T {
		return time_until<T>(start_exec, hres_clock::now());
	}

	inline auto now_string(const std::string_view fmt = DEFAULT_DATE_FORMAT) -> std::string {
		const auto time = std::time(nullptr);

		std::ostringstream os;
		os << std::put_time(std::localtime(&time), fmt.data());

		return os.str();
	}
} // namespace utils::time

#endif /* end of include guard: THANOS_TIME_UTILS_HPP */
