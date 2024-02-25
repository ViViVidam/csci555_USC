/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include "tickets.hpp"

#include <cerrno>      // for errno
#include <cstring>     // for strerror
#include <fstream>     // for ifstream
#include <iostream>    // for operator<<, ofstream, basic_ostream
#include <string_view> // for string_view, operator<<

#include "types.hpp"         // for real_t
#include "utils/verbose.hpp" // for lvl, DEFAULT_LVL, LVL1

namespace migration {
	tickets_t TICKETS_MEM_CELL_WORSE(DEFAULT_TICKETS_MEM_CELL_WORSE, TICKETS_MEM_CELL_WORSE_MASK);
	tickets_t TICKETS_MEM_CELL_NO_DATA(DEFAULT_TICKETS_MEM_CELL_NO_DATA, TICKETS_MEM_CELL_NO_DATA_MASK);
	tickets_t TICKETS_MEM_CELL_BETTER(DEFAULT_TICKETS_MEM_CELL_BETTER, TICKETS_MEM_CELL_BETTER_MASK);
	tickets_t TICKETS_FREE_CORE(DEFAULT_TICKETS_FREE_CORE, TICKETS_FREE_CORE_MASK);
	tickets_t TICKETS_PREF_NODE(DEFAULT_TICKETS_PREF_NODE, TICKETS_PREF_NODE_MASK);
	tickets_t TICKETS_UNDER_PERF(DEFAULT_TICKETS_THREAD_UNDER_PERF, TICKETS_THREAD_UNDER_PERF_MASK);

	real_t PERF_THRESHOLD = DEFAULT_PERF_THRESHOLD;
	real_t UNDO_THRESHOLD = DEFAULT_UNDO_THRESHOLD;

	auto read_tickets_file(const char * const filename_) -> bool {
		std::string_view filename = filename_ == nullptr ? FILE_TICKETS : filename_;

		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << "Parsing tickets option file: " << filename << '\n';
		}

		std::ifstream file(filename.data());

		if (!file.is_open()) {
			if (verbose::print_with_lvl(verbose::LVL1)) {
				std::cerr << "Failed to load migration options (" << strerror(errno) << "), using default parameters."
				          << '\n';
			}
			return false;
		}

		try {
			file >> TICKETS_MEM_CELL_WORSE;
			file >> TICKETS_MEM_CELL_NO_DATA;
			file >> TICKETS_MEM_CELL_BETTER;
			file >> TICKETS_FREE_CORE;
			file >> TICKETS_PREF_NODE;
			file >> TICKETS_UNDER_PERF;

			file >> PERF_THRESHOLD;
			file >> UNDO_THRESHOLD;
		} catch (...) {
			if (verbose::print_with_lvl(verbose::LVL1)) {
				std::cerr << "Failed to load migration options, using default parameters..." << '\n';
			}

			TICKETS_MEM_CELL_WORSE.value(DEFAULT_TICKETS_MEM_CELL_WORSE);
			TICKETS_MEM_CELL_NO_DATA.value(DEFAULT_TICKETS_MEM_CELL_NO_DATA);
			TICKETS_MEM_CELL_BETTER.value(DEFAULT_TICKETS_MEM_CELL_BETTER);
			TICKETS_FREE_CORE.value(DEFAULT_TICKETS_FREE_CORE);
			TICKETS_PREF_NODE.value(DEFAULT_TICKETS_PREF_NODE);
			TICKETS_UNDER_PERF.value(DEFAULT_TICKETS_THREAD_UNDER_PERF);

			PERF_THRESHOLD = DEFAULT_PERF_THRESHOLD;
			UNDO_THRESHOLD = DEFAULT_UNDO_THRESHOLD;

			file.close();

			return false;
		}

		file.close();

		return true;
	}

	auto write_tickets_file(const char * const filename_) -> bool {
		std::string_view filename = filename_ == nullptr ? FILE_TICKETS : filename_;

		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << "Writing tickets option file: " << filename << '\n';
		}

		std::ofstream file(filename.data());

		if (!file.is_open()) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to write tickets option file: " << strerror(errno) << '\n';
			}
			return false;
		}

		try {
			file << TICKETS_MEM_CELL_WORSE << '\n';
			file << TICKETS_MEM_CELL_NO_DATA << '\n';
			file << TICKETS_MEM_CELL_BETTER << '\n';
			file << TICKETS_FREE_CORE << '\n';
			file << TICKETS_PREF_NODE << '\n';
			file << TICKETS_UNDER_PERF << '\n';

			file << PERF_THRESHOLD << '\n';
			file << UNDO_THRESHOLD << '\n';
		} catch (...) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to write migration options..." << '\n';
			}

			file.close();

			return false;
		}

		file.close();

		return true;
	}

	auto write_tickets_csv_header(const char * const filename_) -> bool {
		std::string_view filename = filename_ == nullptr ? FILE_TICKETS : filename_;

		std::ofstream file(filename.data());

		if (!file.is_open()) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to write tickets CSV header: " << strerror(errno) << '\n';
			}
			return false;
		}

		try {
			file << "TICKETS MEM CELL WORSE" << ';';
			file << "TICKETS MEM CELL NO DATA" << ';';
			file << "TICKETS MEM CELL BETTER" << ';';
			file << "TICKETS FREE CORE" << ';';
			file << "TICKETS PREF NODE" << ';';
			file << "TICKETS THREAD UNDER PERF" << ';';

			file << "PERF THRESHOLD" << ';';
			file << "UNDO THRESHOLD" << '\n';
		} catch (...) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to write tickets CSV header..." << '\n';
			}

			file.close();

			return false;
		}

		file.close();

		return true;
	}

	auto write_tickets_csv(const char * const filename_) -> bool {
		std::string_view filename = filename_ == nullptr ? FILE_TICKETS : filename_;

		std::ifstream file_aux(filename.data());

		// If the file does not exist or is empty...
		if (!file_aux.is_open() || std::cmp_equal(file_aux.peek(), std::ifstream::traits_type::eof())) {
			write_tickets_csv_header(filename.data());
		}

		std::ofstream file(filename.data(), std::ofstream::out | std::ofstream::app);

		if (!file.is_open()) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to write tickets CSV file: " << strerror(errno) << '\n';
			}
			return false;
		}

		try {
			file << TICKETS_MEM_CELL_WORSE << ';';
			file << TICKETS_MEM_CELL_NO_DATA << ';';
			file << TICKETS_MEM_CELL_BETTER << ';';
			file << TICKETS_FREE_CORE << ';';
			file << TICKETS_PREF_NODE << ';';
			file << TICKETS_UNDER_PERF << ';';

			file << PERF_THRESHOLD << ';';
			file << UNDO_THRESHOLD << '\n';
		} catch (...) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Failed to write tickets CSV file..." << '\n';
			}

			file.close();

			return false;
		}

		file.close();

		return true;
	}
} // namespace migration