/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_ARITHMETIC_HPP
#define THANOS_ARITHMETIC_HPP

#include <random>      // for random_device, mt19937, uniform_int_distribution
#include <type_traits> // for is_integral

namespace utils::arithmetic {
	// Returns:
	// -1 if val < 0
	// 0 if val = 0
	// 1 if val > 1
	template<typename T>
	[[nodiscard]] inline auto sgn(const T val) -> T {
		if constexpr (std::is_signed_v<T>) {
			return (T(0) < val) - (val < T(0));
		} else {
			return val == T(0) ? T(0) : T(1);
		}
	}

	// Return a random value in the range [min, max)
	template<typename T>
	[[nodiscard]] inline auto rnd(const T min = T(), const T max = T(1)) -> T {
		static std::random_device rnd_dev;
		static std::mt19937       gen(rnd_dev());

		// Return an integral if T is of type integral
		if constexpr (std::is_integral<T>()) {
			std::uniform_int_distribution<T> dis(min, max - 1);
			return dis(gen);
		} else {
			// Otherwise, return a real value
			std::uniform_real_distribution<T> dis(min, max);
			return dis(gen);
		}
	}
} // namespace utils::arithmetic

#endif /* end of include guard: THANOS_ARITHMETIC_HPP */
