/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_STRING_HPP
#define THANOS_STRING_HPP

#include <algorithm> // for any_of
#include <cmath>     // for isnormal
#include <sstream>   // for ostringstream, basic_ostream::operator<<, basic_i...
#include <string>    // for string
#include <utility>   // for std::cmp_greater_equal

namespace utils::string {
	[[nodiscard]] inline auto is_number(const std::string & s) -> bool {
		return !s.empty() && std::any_of(s.begin(), s.end(), [](const auto c) { return !std::isdigit(c); });
	}

	// This function is *very* slow, so mind its use... Waiting for compilers to implement the std::format collection of
	// functions (supposed to be much more efficient)
	template<typename T>
	inline auto to_string(const T a_value, const int precision = 2) -> std::string {
		char buffer[64];
		int  ret = 0;

		if constexpr (std::is_integral_v<T>) {
			if constexpr (std::is_signed_v<T>) {
				ret = snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long int>(a_value));
			} else {
				ret = snprintf(buffer, sizeof(buffer), "%llu", static_cast<long long unsigned>(a_value));
			}
		} else {
			ret = snprintf(buffer, sizeof(buffer), "%.*f", precision, a_value);
		}

		return std::cmp_greater_equal(ret, 0) ? std::string(buffer) : std::to_string(a_value);
	}

	template<typename T>
	inline auto to_string_hex(const T address) -> std::string {
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "0x%lx", address);
		return { buffer };
	}

	template<typename T, typename U>
	inline auto percentage(const T num, const U den, const int precision = 2) -> std::string {
		return std::isnormal(den) ? to_string(100.0 * num / den, precision) : to_string(0, precision);
	}

	template<typename T>
	inline auto percentage(const T num, const int precision = 2) -> std::string {
		return to_string(100.0 * num, precision);
	}
} // namespace utils::string

#endif /* end of include guard: THANOS_STRING_HPP */
