/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_SAMPLES_HPP
#define THANOS_SAMPLES_HPP

#include <array>         // for array
#include <cstdint>       // for uint64_t, uint8_t, uint32_t
#include <iostream>      // for operator<<, ostream, basic_os...
#include <sys/types.h>   // for pid_t, size_t
#include <unordered_set> // for operator==, set, _Rb_tree_con...
#include <vector>        // for vector

#include "system_info/system_info.hpp" // for INVALID_CPU
#include "utils/types.hpp"             // for real_t

namespace samples {
	enum sample_type_t {
		MEM_SAMPLE = 0,
		REQ_SAMPLE,
		INS_SAMPLE,
#ifndef JUST_INS
		FP_SCALAR_SAMPLE, // multiply by 1 to get flops
		FP_128B_D_SAMPLE, // multiply by 2 to get flops
		FP_128B_S_SAMPLE, // multiply by 4 to get flops
		FP_256B_D_SAMPLE, // multiply by 4 to get flops
		FP_256B_S_SAMPLE, // multiply by 8 to get flops
#ifdef USE_512B_INS
		FP_512B_D_SAMPLE, // multiply by 8 to get flops
		FP_512B_S_SAMPLE, // multiply by 16 to get flops
#endif
#endif
		NUM_TYPES
	};

	static constexpr uint8_t MEM_SAMPLE_MULT = 1;
	static constexpr uint8_t REQ_SAMPLE_MULT = 1;
	static constexpr uint8_t INS_SAMPLE_MULT = 1;
#ifndef JUST_INS
	static constexpr uint8_t FP_SCALAR_SAMPLE_MULT = 1; // multiply by 1 to get flops
	static constexpr uint8_t FP_128B_D_SAMPLE_MULT = 2; // multiply by 2 to get flops
	static constexpr uint8_t FP_128B_S_SAMPLE_MULT = 4; // multiply by 4 to get flops
	static constexpr uint8_t FP_256B_D_SAMPLE_MULT = 4; // multiply by 4 to get flops
	static constexpr uint8_t FP_256B_S_SAMPLE_MULT = 8; // multiply by 8 to get flops
#ifdef USE_512B_INS
	static constexpr uint8_t FP_512B_D_SAMPLE_MULT = 8;  // multiply by 8 to get flops
	static constexpr uint8_t FP_512B_S_SAMPLE_MULT = 16; // multiply by 16 to get flops
#endif
#endif

	static constexpr int NUM_GROUPS = NUM_TYPES;

	inline auto to_str(const sample_type_t & type) -> const char * {
		switch (type) {
			case MEM_SAMPLE:
				return "MEM_SAMPLE";
			case REQ_SAMPLE:
				return "REQ_SAMPLE";
			case INS_SAMPLE:
				return "INS_SAMPLE";
#ifndef JUST_INS
			case FP_SCALAR_SAMPLE:
				return "FP_SCALAR_SAMPLE";
			case FP_128B_D_SAMPLE:
				return "FP_128B_D_SAMPLE";
			case FP_128B_S_SAMPLE:
				return "FP_128B_S_SAMPLE";
			case FP_256B_D_SAMPLE:
				return "FP_256B_D_SAMPLE";
			case FP_256B_S_SAMPLE:
				return "FP_256B_S_SAMPLE";
#ifdef USE_512B_INS
			case FP_512B_D_SAMPLE:
				return "FP_512B_D_SAMPLE";
			case FP_512B_S_SAMPLE:
				return "FP_512B_S_SAMPLE";
#endif
#endif
			default:
				return "UNKNOWN";
		}
	}

	extern uset<pid_t> PIDs_to_filter;

	static constexpr bool filter_by_PIDs = true;

	template<template<typename...> typename Iterable>
	inline void update_PIDs_to_filter(const Iterable<pid_t> & pids) {
		// Clear the list of PIDs to filter (to purge those not valid anymore)
		PIDs_to_filter.clear();
		// And insert the new ones.
		PIDs_to_filter.insert(pids.begin(), pids.end());
	}

	inline void insert_PID_to_filter(const pid_t & pid) {
		// And insert the new ones.
		PIDs_to_filter.insert(pid);
	}

	[[nodiscard]] inline auto accept_PID_filter(const pid_t tid) -> bool {
		return PIDs_to_filter.contains(tid);
	}

	[[maybe_unused]] inline void print_filter() {
		std::cout << "Filter set:" << '\n';
		for (const auto & tid : PIDs_to_filter) {
			std::cout << "\tPID: " << tid << '\n';
		}
	}

	inline auto type_multiplier(const sample_type_t & type) -> uint8_t {
		switch (type) {
			case MEM_SAMPLE:
				return MEM_SAMPLE_MULT;
			case REQ_SAMPLE:
				return REQ_SAMPLE_MULT;
			case INS_SAMPLE:
				return INS_SAMPLE_MULT;
#ifndef JUST_INS
			case FP_SCALAR_SAMPLE:
				return FP_SCALAR_SAMPLE_MULT;
			case FP_128B_D_SAMPLE:
				return FP_128B_D_SAMPLE_MULT;
			case FP_128B_S_SAMPLE:
				return FP_128B_S_SAMPLE_MULT;
			case FP_256B_D_SAMPLE:
				return FP_256B_D_SAMPLE_MULT;
			case FP_256B_S_SAMPLE:
				return FP_256B_S_SAMPLE_MULT;
#ifdef USE_512B_INS
			case FP_512B_D_SAMPLE:
				return FP_512B_D_SAMPLE_MULT;
			case FP_512B_S_SAMPLE:
				return FP_512B_S_SAMPLE_MULT;
#endif
#endif
			default:
				return 1;
		}
	}

	namespace details {
		static constexpr std::array<const char * const, NUM_GROUPS> events = {
			"MEM_TRANS_RETIRED:LATENCY_ABOVE_THRESHOLD",
			"OFFCORE_REQUESTS:ALL_DATA_RD",
			"INST_RETIRED"
#ifndef JUST_INS
			,
			"FP_ARITH:SCALAR_DOUBLE:SCALAR_SINGLE",
			"FP_ARITH:128B_PACKED_DOUBLE",
			"FP_ARITH:128B_PACKED_SINGLE",
			"FP_ARITH:256B_PACKED_DOUBLE",
			"FP_ARITH:256B_PACKED_SINGLE"
#ifdef USE_512B_INS
			,
			"FP_ARITH:512B_PACKED_DOUBLE",
			"FP_ARITH:512B_PACKED_SINGLE"
#endif
#endif
		};
	} // namespace details

	class pebs {
	public:
		uint64_t iip{};          // Instruction pointer.
		pid_t    pid{};          // Process ID.
		pid_t    tid{};          // Thread ID.
		uint64_t time{};         // Timestamp (nanoseconds).
		uint64_t sample_addr{};  // Address, if applicable.
		uint32_t cpu{};          // CPU where the samples was generated.
		uint64_t weight{};       // Hardware provided weight value that expresses how costly the sampled event was.
		                         // This allows the hardware to highlight expensive events in a profile.
		uint64_t time_enabled{}; // Time event active (nanoseconds).
		uint64_t time_running{}; // Time event on CPU (nanoseconds).
		uint64_t dsrc{};         // Records the data source: where in the memory hierarchy the data associated with
		// the sampled instruction came from. This is available only if the underlying hardware supports this feature.

		uint64_t value{}; // Value of the sample.

		sample_type_t sample_type; // Sample type as defined in "enum sample_type_t".
		uint8_t       multiplier;  // Multiplier according to sample type.

		pebs() = delete;

		pebs(const pebs & p) noexcept = default;
		pebs(pebs && p) noexcept      = default;

		explicit pebs(sample_type_t sample_type) :
		    sample_type(sample_type), multiplier(samples::type_multiplier(sample_type)) {
		}

		[[nodiscard]] inline auto is_mem_sample() const -> bool {
			return sample_type == MEM_SAMPLE;
		}

		[[nodiscard]] inline auto is_req_sample() const -> bool {
			return sample_type == REQ_SAMPLE;
		}

		[[nodiscard]] inline auto type() const -> sample_type_t {
			return sample_type;
		}

		inline friend auto operator<<(std::ostream & os, const pebs & p) -> std::ostream & {
			/* clang-format off */
			os << "IIP: "            << p.iip                 << '\n';
			os << "\tType: "         << to_str(p.sample_type) << '\n';
			os << "\tPID: "          << p.pid                 << '\n';
			os << "\tTID: "          << p.tid                 << '\n';
			os << "\tCPU: "          << p.cpu                 << '\n';
			os << "\tDSRC: "         << p.dsrc                << '\n';
			os << "\tTime: "         << p.time                << '\n';
			os << "\tAddr: "         << p.sample_addr         << '\n';
			os << "\tWeight: "       << p.weight              << '\n';
			os << "\tTime enabled: " << p.time_enabled        << '\n';
			os << "\tTime running: " << p.time_running        << '\n';
			os << "\tValue: "        << p.value               << '\n';
			/* clang-format on */

			return os;
		}
	};
} // namespace samples

#endif /* end of include guard: THANOS_SAMPLES_HPP */
