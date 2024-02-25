/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_I_SAMPLE_HPP
#define THANOS_I_SAMPLE_HPP

#include <iostream>    // for operator<<, basic_ostream::operator<<
#include <sys/types.h> // for pid_t

#include "utils/types.hpp" // for tim_t, cpu_t

class i_sample_t {
private:
	cpu_t cpu_;
	pid_t pid_;
	pid_t tid_;
	tim_t time_;

	// Constructors are protected so this base class can be used *only* when inheriting from it
public:
	i_sample_t() = delete;

protected:
	i_sample_t(const i_sample_t & d) noexcept = default;

	i_sample_t(i_sample_t && d) noexcept = default;

	i_sample_t(const cpu_t cpu, const pid_t pid, const pid_t tid, const tim_t time) :
	    cpu_(cpu), pid_(pid), tid_(tid), time_(time) {
	}

public:
	[[nodiscard]] inline auto cpu() const -> cpu_t {
		return cpu_;
	}

	[[nodiscard]] inline auto pid() const -> pid_t {
		return pid_;
	}

	[[nodiscard]] inline auto tid() const -> pid_t {
		return tid_;
	}

	[[nodiscard]] inline auto time() const -> tim_t {
		return time_;
	}

	inline void time(tim_t time) {
		time_ = time;
	}

	friend auto operator<<(std::ostream & os, const i_sample_t & d) -> std::ostream & {
		os << "CPU: " << d.cpu_ << ", PID: " << d.pid_ << ", TID: " << d.tid_ << ", TIME: " << d.time_;
		return os;
	}
};

#endif /* end of include guard: THANOS_I_SAMPLE_HPP */
