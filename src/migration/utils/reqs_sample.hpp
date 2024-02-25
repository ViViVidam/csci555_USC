/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_REQS_SAMPLE_HPP
#define THANOS_REQS_SAMPLE_HPP

#include <iostream>    // for operator<<, basic_ostream::...
#include <sys/types.h> // for pid_t

#include "migration/utils/i_sample.hpp"  // for data_cell_t
#include "utils/types.hpp"               // for req_t, cpu_t, tim_t

class reqs_sample_t : public i_sample_t {
private:
	req_t reqs_;

public:
	reqs_sample_t() = delete;

	reqs_sample_t(const reqs_sample_t & d) noexcept = default;

	reqs_sample_t(reqs_sample_t && d) noexcept = default;

	reqs_sample_t(const cpu_t cpu, const pid_t pid, const pid_t tid, const tim_t time, const req_t reqs) :
	    i_sample_t(cpu, pid, tid, time), reqs_(reqs){};

	[[nodiscard]] inline auto reqs() const {
		return reqs_;
	}

	inline void reqs(req_t reqs) {
		reqs_ = reqs;
	}

	inline friend auto operator<<(std::ostream & os, const reqs_sample_t & i) -> std::ostream & {
		os << "CPU: " << i.cpu() << ", PID: " << i.pid() << ", TID: " << i.tid() << ", REQS: " << i.reqs()
		   << ", TIME: " << i.time();
		return os;
	}
};

#endif /* end of include guard: THANOS_REQS_SAMPLE_HPP */