/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEMORY_MIG_STRATS_HPP
#define THANOS_MEMORY_MIG_STRATS_HPP

#include <fstream>  // for ifstream
#include <iostream> // for operator<<, basic_ostream, basic_ostrea...
#include <string>   // for operator<<, allocator, string

#include "utils/verbose.hpp" // for DEFAULT_LVL, lvl

namespace migration::memory {
	enum strategy_t {
		TMMA,
		LMMA,
		RMMA,
	};

	static constexpr strategy_t DEFAULT_STRATEGY = LMMA;

	[[nodiscard]] static auto parse_strategy_file(const char * filename = "strategy_option") -> strategy_t {
		std::ifstream file(filename);

		if (!file.is_open()) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to strategy options (file might not exist), using default..." << '\n';
			}
			return DEFAULT_STRATEGY;
		}

		int strategy = DEFAULT_STRATEGY;

		file >> strategy;

		file.close();

		switch (strategy) {
			case TMMA:
				return TMMA;
			case LMMA:
				return LMMA;
			case RMMA:
				return RMMA;
			default:
				return DEFAULT_STRATEGY;
		}
	}

	[[nodiscard]] static auto print_strategy(const strategy_t strategy) -> const char * {
		switch (strategy) {
			case TMMA:
				return "TMMA (Threshold Memory Migration Algorithm).";
			case RMMA:
				return "RMMA (Random Memory Migration Algorithm).";
			case LMMA:
				return "LMMA (Latency Memory Migration Algorithm).";
			default:
				return print_strategy(DEFAULT_STRATEGY);
		}
	}

	static auto print_strategies(std::ostream & os, const std::string & prelude = "") -> std::ostream & {
		os << prelude << TMMA << ": " << print_strategy(TMMA) << '\n';
		os << prelude << LMMA << ": " << print_strategy(LMMA) << '\n';
		os << prelude << RMMA << ": " << print_strategy(RMMA) << '\n';
		return os;
	}
} // namespace migration::memory

#endif /* end of include guard: THANOS_MEMORY_MIG_STRATS_HPP */
