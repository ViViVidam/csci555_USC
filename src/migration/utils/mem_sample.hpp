/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MEM_SAMPLE_HPP
#define THANOS_MEM_SAMPLE_HPP

#include <iostream>           // for operator<<, ostream, cout
#include <linux/perf_event.h> // for perf_mem_data_src, perf_mem...
#include <string>             // for operator<<
#include <sys/types.h>        // for pid_t

#include "migration/utils/i_sample.hpp" // for data_cell_t
#include "system_info/memory_info.hpp"   // for page_from_addr
#include "utils/string.hpp"              // for to_string_hex
#include "utils/types.hpp"               // for addr_t, req_t, lat_t, node_t

class memory_sample_t : public i_sample_t {
private:
	req_t  reqs_;      // Number of memory operations performed from CPU
	addr_t addr_;      // Address of the sampled memory operation
	addr_t page_;      // Address of the beginning of the page
	lat_t  latency_;   // Latency of the sampled operation
	size_t pagesize_;  // Size of the page (typically 4kB for small pages, 2MB to 1GB for Transparent Huge Pages)
	dsrc_t dsrc_;      // Code to know the level in the memory hierarchy where the sample was produced
	node_t page_node_; // Node in which the page was located when sample was processed

public:
	memory_sample_t() = delete;

	memory_sample_t(const memory_sample_t & d) noexcept = default;

	memory_sample_t(memory_sample_t && d) noexcept = default;

	memory_sample_t(const cpu_t cpu, const pid_t pid, const pid_t tid, const tim_t time, const req_t reqs,
	                   const addr_t addr, const addr_t page, const lat_t latency, const size_t pagesize,
	                   const dsrc_t dsrc, const node_t page_node) :
	    i_sample_t(cpu, pid, tid, time),
	    reqs_(reqs),
	    addr_(addr),
	    page_(page),
	    latency_(latency),
	    pagesize_(pagesize),
	    dsrc_(dsrc),
	    page_node_(page_node){};

	[[nodiscard]] inline auto is_cache_miss() const -> bool {
		const auto * const mdsrc = reinterpret_cast<const perf_mem_data_src *>(&dsrc_);

		return mdsrc->mem_lvl & PERF_MEM_LVL_MISS;
	}

	[[nodiscard]] inline auto latency() const -> lat_t {
		return latency_;
	}

	[[nodiscard]] inline auto addr() const -> addr_t {
		return addr_;
	}

	[[nodiscard]] inline auto page() const -> addr_t {
		return page_;
	}

	[[nodiscard]] inline auto page_node() const -> node_t {
		return page_node_;
	}

	[[nodiscard]] inline auto pagesize() const -> size_t {
		return pagesize_;
	}

	[[nodiscard]] inline auto reqs() const -> req_t {
		return reqs_;
	}

	inline void reqs(req_t reqs) {
		reqs_ = reqs;
	}

	void print_dsrc() const {
		const auto * const mdsrc = reinterpret_cast<const perf_mem_data_src *>(&dsrc_);

		std::cout << "mem_op: " << mdsrc->mem_op << " ->\n";
		if (mdsrc->mem_op & PERF_MEM_OP_NA) std::cout << "\tnot available\n";
		if (mdsrc->mem_op & PERF_MEM_OP_LOAD) std::cout << "\tload instruction\n";
		if (mdsrc->mem_op & PERF_MEM_OP_STORE) std::cout << "\tstore instruction\n";
		if (mdsrc->mem_op & PERF_MEM_OP_PFETCH) std::cout << "\tprefetch\n";
		if (mdsrc->mem_op & PERF_MEM_OP_EXEC) std::cout << "\tcode (execution)\n";

		std::cout << "mem_lvl: " << mdsrc->mem_lvl << " ->\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_NA) std::cout << "\tnot available\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_HIT) std::cout << "\thit level\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_MISS) std::cout << "\tmiss level\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_L1) std::cout << "\tL1\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_LFB) std::cout << "\tLine Fill Buffer\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_L2) std::cout << "\tL2\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_L3) std::cout << "\tL3\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_LOC_RAM) std::cout << "\tLocal DRAM\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_REM_RAM1) std::cout << "\tRemote DRAM (1 hop)\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_REM_RAM2) std::cout << "\tRemote DRAM (2 hops)\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_REM_CCE1) std::cout << "\tRemote Cache (1 hop)\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_REM_CCE2) std::cout << "\tRemote Cache (2 hops)\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_IO) std::cout << "\tI/O memory\n";
		if (mdsrc->mem_lvl & PERF_MEM_LVL_UNC) std::cout << "\tUncached memory\n";

		std::cout << "mem_snoop: " << mdsrc->mem_snoop << " ->\n";
		if (mdsrc->mem_snoop & PERF_MEM_SNOOP_NA) std::cout << "\tnot available\n";
		if (mdsrc->mem_snoop & PERF_MEM_SNOOP_NONE) std::cout << "\tno snoop\n";
		if (mdsrc->mem_snoop & PERF_MEM_SNOOP_HIT) std::cout << "\tsnoop hit\n";
		if (mdsrc->mem_snoop & PERF_MEM_SNOOP_MISS) std::cout << "\tsnoop miss\n";
		if (mdsrc->mem_snoop & PERF_MEM_SNOOP_HITM) std::cout << "\tsnoop hit modified\n";

		std::cout << "mem_lock: " << mdsrc->mem_lock << " ->\n";
		if (mdsrc->mem_lock & PERF_MEM_LOCK_NA) std::cout << "\tnot available\n";
		if (mdsrc->mem_lock & PERF_MEM_LOCK_LOCKED) std::cout << "\tlocked transaction\n";

		std::cout << "mem_dtlb: " << mdsrc->mem_dtlb << " ->\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_NA) std::cout << "\tnot available\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_HIT) std::cout << "\thit level\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_MISS) std::cout << "\tmiss level\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_L1) std::cout << "\tL1\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_L2) std::cout << "\tL2\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_WK) std::cout << "\tHardware Walker\n";
		if (mdsrc->mem_dtlb & PERF_MEM_TLB_OS) std::cout << "\tOS fault handler\n";
	}

	friend auto operator<<(std::ostream & os, const memory_sample_t & m) -> std::ostream & {
		os << "CPU: " << m.cpu() << ", PID: " << m.pid() << ", TID: " << m.tid()
		   << ", ADDR: " << utils::string::to_string_hex(m.addr()) << ", LATENCY: " << m.latency()
		   << ", DSRC: " << m.dsrc_ << ", TIME: " << m.time();
		return os;
	}
};

#endif /* end of include guard: THANOS_MEM_SAMPLE_HPP */
