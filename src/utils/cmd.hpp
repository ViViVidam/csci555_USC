/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_CMD_HPP
#define THANOS_CMD_HPP

#include <array>       // for array
#include <cstdio>      // for pclose, popen, FILE, fgets, size_t
#include <memory>      // for operator==, unique_ptr
#include <stdexcept>   // for runtime_error
#include <string>      // for string, operator+
#include <string_view> // for string_view

namespace utils::cmd {
	// taken from https://stackoverflow.com/a/478960
	template<size_t buff_length = 128>
	inline auto exec(std::string_view cmd, const bool truncate_final_newlines = true) -> std::string {
		std::array<char, buff_length> buffer{};

		std::string result;

		std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.data(), "r"), pclose);

		if (pipe == nullptr) { throw std::runtime_error("Could not execute command " + std::string(cmd)); }

		while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
			result += buffer.data();
		}

		if (truncate_final_newlines) {
			// Remove last '\n' (if there is one)
			while (result[result.length() - 1] == '\n') {
				result.pop_back();
			}
		}

		return result;
	}
} // namespace utils::cmd

#endif /* end of include guard: THANOS_CMD_HPP */
