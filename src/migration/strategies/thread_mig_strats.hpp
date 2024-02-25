/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_THREAD_MIG_STRATS_HPP
#define THANOS_THREAD_MIG_STRATS_HPP

#include <iostream> // for operator<<, basic_ostream, basic_ostrea...
#include <string>   // for operator<<, allocator, char_traits, string

#include "utils/verbose.hpp" // for DEFAULT_LVL, lvl

namespace migration::thread {
	enum strategy_t {
		LBMA,
		CIMAR,
		NIMAR,
		IMAR2,
		RANDOM,
		RM3D,
		Annealing_node,
	};

	static constexpr strategy_t DEFAULT_STRATEGY = NIMAR;

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
			case LBMA:
				return LBMA;
			case CIMAR:
				return CIMAR;
			case IMAR2:
				return IMAR2;
			case NIMAR:
				return NIMAR;
			case RANDOM:
				return RANDOM;
			case RM3D:
				return RM3D;
			case Annealing_node:
				return Annealing_node;
			default:
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Strategy not found, using default strategy..." << '\n';
				}
				return DEFAULT_STRATEGY;
		}
	}

	static auto print_strategies(std::ostream & os, const std::string & prelude = "") -> std::ostream & {
		os << prelude << LBMA << ": "
		   << "LBMA (Lottery-Based Migration Algorithm)." << '\n';
		os << prelude << CIMAR << ": "
		   << "CIMAR (Core Interchange and Migration Algorithm with performance Record)." << '\n';
		os << prelude << NIMAR << ": "
		   << "NIMAR (Node Interchange and Migration Algorithm with performance Record)." << '\n';
		os << prelude << IMAR2 << ": "
		   << "IMAR^2 (Interchange and Migration Algorithm with performance Record and Rollback)." << '\n';
		os << prelude << RANDOM << ": "
		   << "Random." << '\n';
		os << prelude << RM3D << ": "
		   << "RM3D." << '\n';
		os << prelude << Annealing_node << ": "
		   << "Annealing node." << '\n';
		return os;
	}

	[[nodiscard]] static auto print_strategy(const strategy_t strategy) -> const char * {
		switch (strategy) {
			case LBMA:
				return "LBMA (Lottery-Based Migration Algorithm)";
			case RANDOM:
				return "Random";
			case RM3D:
				return "RM3D";
			case Annealing_node:
				return "Annealing node";
			case NIMAR:
				return "NIMAR (Node Interchange and Migration Algorithm with performance Record)";
			case IMAR2:
				return "IMAR^2 (Interchange and Migration Algorithm with performance Record and Rollback)";
			case CIMAR:
				return "CIMAR (Core Interchange and Migration Algorithm with performance Record)";
			default:
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Strategy not found, using default strategy..." << '\n';
				}
				return print_strategy(DEFAULT_STRATEGY);
		}
	}
} // namespace migration::thread

#endif /* end of include guard: THANOS_THREAD_MIG_STRATS_HPP */
