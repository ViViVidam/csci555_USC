/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_INST_SAMPLE_HPP
#define THANOS_INST_SAMPLE_HPP

#include <cstdint>     // for uint8_t
#include <iostream>    // for operator<<, basic_ostream::...
#include <sys/types.h> // for pid_t

#include "migration/utils/i_sample.hpp"  // for data_cell_t
#include "utils/types.hpp"               // for ins_t, cpu_t, tim_t

class inst_sample_t : public i_sample_t {
private:
	ins_t inst_;

	uint8_t multiplier_;
	bool    flop_;

public:
	inst_sample_t() = delete;

	inst_sample_t(const inst_sample_t & d) noexcept = default;

	inst_sample_t(inst_sample_t && d) noexcept = default;

	inst_sample_t(const cpu_t cpu, const pid_t pid, const pid_t tid, const tim_t time, const ins_t inst,
	                 const uint8_t multiplier, const bool flop = false) :
	    i_sample_t(cpu, pid, tid, time), inst_(inst), multiplier_(multiplier), flop_(flop){};

	[[nodiscard]] inline auto inst() const -> ins_t {
		return inst_;
	}

	inline void inst(ins_t inst) {
		inst_ = inst;
	}

	[[nodiscard]] inline auto multiplier() const -> uint8_t {
		return multiplier_;
	}

	[[nodiscard]] inline auto flop() const -> bool {
		return flop_;
	}

	inline friend auto operator<<(std::ostream & os, const inst_sample_t & i) -> std::ostream & {
		os << "CPU: " << i.cpu() << ", PID: " << i.pid() << ", TID: " << i.tid() << ", INST: " << i.inst_
		   << ", TIME: " << i.time();
		return os;
	}
};

#endif /* end of include guard: THANOS_INST_SAMPLE_HPP */