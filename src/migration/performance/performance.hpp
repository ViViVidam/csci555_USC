/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_PERFORMANCE_HPP
#define THANOS_PERFORMANCE_HPP

#include "utils/types.hpp" // for real_t

namespace performance {
	static constexpr real_t PERFORMANCE_INVALID_VALUE = -1;
	static constexpr real_t NEGLIGIBLE_PERFORMANCE    = 1e-3;
} // namespace performance

#endif /* end of include guard: THANOS_PERFORMANCE_HPP */
