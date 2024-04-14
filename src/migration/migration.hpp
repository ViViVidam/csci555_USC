/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_MIGRATION_HPP
#define THANOS_MIGRATION_HPP

#include <array>         // for array
#include <cstdint>       // for int64_t
#include <features.h>    // for __glibc_unlikely
#include <iostream>      // for operator<<
#include <string>        // for operator<<
#include <sys/types.h>   // for size_t, pid_t
#include <type_traits>   // for add_const<>::type
#include <unordered_map> // for unordered_map
#include <unordered_set> // for unordered_set
#include <utility>       // for pair, make_pair
#include <vector>        // for vector, allocator

#include "migration/migration_cell.hpp"               // for migration_cell
#include "migration/migration_var.hpp"                // for perf_table
#include "migration/performance/mempages_table.hpp"   // for mempages_table
#include "migration/performance/tid_perf_table.hpp"   // for tid_perf_table
#include "migration/strategies/thread_strategy.hpp"   // for Istrategy
#include "migration/utils/inst_sample.hpp"            // for inst_sample_t
#include "migration/utils/mem_sample.hpp"             // for memory_data_ce...
#include "migration/utils/reqs_sample.hpp"            // for reqs_sample_t
#include "migration/utils/times.hpp"                  // for get_time_value
#include "samples/samples.hpp"                        // for pebs, NUM_GROUPS
#include "strategies/memory_mig_strats.hpp"           // for parse_strategy...
#include "strategies/memory_strats/lmma.hpp"          // for lmma
#include "strategies/memory_strats/rmma.hpp"          // for rmma
#include "strategies/memory_strats/tmma.hpp"          // for tmma
#include "strategies/thread_mig_strats.hpp"           // for parse_strategy...
#include "strategies/thread_strats/annealing.hpp"     // for annealing
#include "strategies/thread_strats/cimar.hpp"         // for cimar
#include "strategies/thread_strats/imar2.hpp"         // for imar2
#include "strategies/thread_strats/lbma.hpp"          // for lbma
#include "strategies/thread_strats/nimar.hpp"         // for nimar
#include "strategies/thread_strats/random.hpp"        // for random
#include "strategies/thread_strats/rm3d_strategy.hpp" // for rm3d_strategy
#include "system_info/memory_info.hpp"                // for get_page_curre...
#include "system_info/system_info.hpp"                // for num_of_cpus
#include "utils/string.hpp"                           // for percentage
#include "utils/time.hpp"                             // for time_until
#include "utils/types.hpp"                            // for addr_t, node_t
#include "utils/verbose.hpp"                          // for lvl, DEFAULT_LVL

namespace migration {
	namespace memory {
		static auto current_strategy(const strategy_t strat = strategy) -> std::unique_ptr<Istrategy> {
			switch (strat) {
				case TMMA:
					return { std::unique_ptr<Istrategy>(new tmma) };
				case RMMA:
					return { std::unique_ptr<Istrategy>(new rmma) };
				case LMMA:
					return { std::unique_ptr<Istrategy>(new lmma) };
				default:
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Incorrect migration strategy" << '\n';
					}
					return current_strategy(DEFAULT_STRATEGY);
			}
		}

		inline void clear_data() {
			perf_table.clear_it();
			num_mem_samples_it = 0;
		}

		inline auto perform_strategy(const time_point & current_time) -> bool {
			migration::memory::last_mig_time = current_time;

			if (verbose::print_with_lvl(verbose::LVL4)) {
				std::cout << "Gathered " << num_mem_samples_it << " memory samples." << '\n';
				if (verbose::print_with_lvl(verbose::LVL_MAX)) {
					std::cout << "Memory pages table: " << '\n' << perf_table << '\n';
				}
			}

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				const auto total_pages       = memory_info::fake_thp_enabled() ? memory_info::n_thps_all_regions() :
				                                                                 memory_info::n_pages_all_regions();
				const auto pages_enough_info = perf_table.pages_with_enough_info();

				const auto * const units = memory_info::fake_thp_enabled() ? "fTHPs" : "Pages";

				std::cout
				    << "#Pages according to /proc: " << utils::string::to_string(memory_info::n_pages_all_regions(), 0)
				    << ". ";
				if (memory_info::fake_thp_enabled()) {
					std::cout << "#fTHPs derived from /proc: "
					          << utils::string::to_string(memory_info::n_thps_all_regions(), 0) << ". ";
				}
				std::cout << "#" << units << " with >0 samples: " << utils::string::to_string(perf_table.size(), 0)
				          << " (" << utils::string::percentage(perf_table.size(), total_pages, 2) << "%). "
				          << "#" << units << " with >"
				          << utils::string::to_string(performance::mempages_table::threshold_enough_info(), 0)
				          << " samples: " << utils::string::to_string(pages_enough_info, 0) << " ("
				          << utils::string::percentage(pages_enough_info, total_pages, 2) << "%)" << '\n';
			}

			// Perform strategy
			if (portion_memory_migrations > 0) {
				auto strat = current_strategy(strategy);
				auto beg   = std::chrono::high_resolution_clock::now();
				strat->migrate();
				auto end = std::chrono::high_resolution_clock::now();
				auto exec_t = std::chrono::duration_cast<std::chrono::microseconds>(end-beg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "exec_time: " << exec_t.count() << std::endl;
				}
			}

			clear_data();

			return true;
		}
	} // namespace memory

	namespace thread {
		static auto current_strategy(const strategy_t strat = strategy) -> std::unique_ptr<Istrategy> {
			switch (strat) {
				case LBMA:
					return { std::unique_ptr<Istrategy>(new lbma) };
				case RANDOM:
					return { std::unique_ptr<Istrategy>(new random) };
				case RM3D:
					return { std::unique_ptr<Istrategy>(new rm3d_strategy) };
				case Annealing_node:
					return { std::unique_ptr<Istrategy>(new annealing_node) };
				case NIMAR:
					return { std::unique_ptr<Istrategy>(new nimar) };
				case IMAR2:
					return { std::unique_ptr<Istrategy>(new imar2) };
				case CIMAR:
					return { std::unique_ptr<Istrategy>(new cimar) };
				default:
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Incorrect migration strategy" << '\n';
					}
					return current_strategy(DEFAULT_STRATEGY);
			}
		}

		inline void clear_data() {
			perf_table.clear_it();
			num_mem_samples_it = 0;
			num_req_samples_it = 0;
			num_ins_samples_it = 0;
		}

		inline auto perform_strategy(const time_point & current_time) -> bool {
			migration::thread::last_mig_time = current_time;

			if (verbose::print_with_lvl(verbose::LVL2)) {
				perf_table.update();

				std::cout << "Gathered " << num_mem_samples_it << " memory samples." << '\n';
				std::cout << "Gathered " << num_req_samples_it << " requests samples." << '\n';
				std::cout << "Gathered " << num_ins_samples_it << " instruction samples." << '\n';
				std::cout << "Performance table: " << '\n' << perf_table << '\n';
			}

			if (std::cmp_greater(max_thread_migrations, 0)) {
				perf_table.update();

				auto strat = current_strategy(strategy);
				strat->migrate();
			}

			clear_data();

			return true;
		}
	} // namespace thread

	inline void end() {
		if (verbose::print_with_lvl(verbose::LVL1)) {
			std::cout << thread::total_migrations << " (" << thread::total_migrations_undone
			          << ") thread migrations done (undone)." << '\n';
			std::cout << memory::total_migrations << " (" << memory::total_migrations_undone
			          << ") memory migrations done (undone)." << '\n';
		}
	}

	template<typename Map>
	static auto process_memory_sample(const samples::pebs & sample, const real_t ageing_factor, Map & page_node_map)
	    -> bool {
		// IMPORTANT!!!!
		// As memory samples are not trustable given its nature (out-of-order execution, 1 sample = 1 address, ...)
		// it is considered sample[i].value = 1 no matter what.
		static_assert(std::is_same<typename Map::key_type, addr_t>::value &&
		                  std::is_same<typename Map::mapped_type, node_t>::value,
		              "Use a map of the form Map<addr_t, node_t>");

		auto page_size = memory_info::page_size(sample.sample_addr);

		auto page_addr   = memory_info::page_from_addr(sample.sample_addr);
		auto region_addr = memory_info::page_from_addr(sample.sample_addr);

		if (memory_info::fake_thp_enabled()) {
			const auto thp_opt = memory_info::fake_thp_from_address(sample.sample_addr);
			if (thp_opt.has_value()) {
				const auto & thp = thp_opt.value().get();

				region_addr = thp.start();
				page_size   = thp.n_pages() * memory_info::pagesize;
			}
		}

		node_t page_node = -1;

		const auto & map_it = page_node_map.find(page_addr);

		// [[unlikely]] because the page should already be in the page_node_map
		if (map_it == page_node_map.end()) [[unlikely]] { // = !contains()
			page_node = memory_info::get_page_current_node(page_addr, sample.pid);
			page_node_map.insert({ page_addr, page_node });
		} else {
			page_node = map_it->second;
		}

		if (std::cmp_less(page_node, 0)) { return false; }

		// Due to how sampling works, you only know the number of requests of a given memory page
		// from the number of samples obtained from it. So, one sample -> one memory operation -> one request
		static constexpr req_t reqs = 1;

		memory_sample_t data(static_cast<cpu_t>(sample.cpu), sample.pid, sample.tid,
		                     static_cast<tim_t>(sample.time_running), reqs, sample.sample_addr, region_addr,
		                     static_cast<lat_t>(sample.weight), page_size, sample.dsrc, page_node);

		thread::perf_table.add_data(data);
		++thread::num_mem_samples_it;

		memory::perf_table.add_data(data, ageing_factor);
		++memory::num_mem_samples_it;

		return true;
	}

	inline auto process_req_sample(const samples::pebs & sample) -> bool {
		reqs_sample_t data(static_cast<cpu_t>(sample.cpu), sample.pid, sample.tid,
		                   static_cast<tim_t>(sample.time_running), static_cast<req_t>(sample.value));

		thread::perf_table.add_data(data);
		++thread::num_req_samples_it;

		return true;
	}

	inline auto process_inst_sample(const samples::pebs & sample) -> bool {
		inst_sample_t data(static_cast<cpu_t>(sample.cpu), sample.pid, sample.tid,
		                   static_cast<tim_t>(sample.time_running), static_cast<ins_t>(sample.value),
		                   sample.multiplier);

		thread::perf_table.add_data(data);
		++thread::num_ins_samples_it;

		return true;
	}

	inline auto process_flops_sample(const samples::pebs & sample) -> bool {
		static constexpr bool FLOPS_VECTOR_INST = true;

		inst_sample_t data(static_cast<cpu_t>(sample.cpu), sample.pid, sample.tid,
		                   static_cast<tim_t>(sample.time_running), static_cast<ins_t>(sample.value), sample.multiplier,
		                   FLOPS_VECTOR_INST);

		thread::perf_table.add_data(data);
		++thread::num_ins_samples_it;

		return true;
	}

	// Pre-compute a map to know where is located each page -> map[addr] = node;
	// Making a single call to retrieve this information for several pages at a time
	// should be more efficient than a call for each page.
	static auto pages_node_map(const std::vector<samples::pebs> & samples) -> umap<addr_t, node_t> {
		static size_t last_map_size = 0;

		umap<pid_t, uset<addr_t>> pid_pages_map;

		for (const auto & sample : samples) {
			if (sample.is_mem_sample()) {
				const auto page_addr = memory_info::page_from_addr(sample.sample_addr);
				pid_pages_map[sample.tid].insert(page_addr);
			}
		}

		umap<addr_t, node_t> page_node_map;
		page_node_map.reserve(last_map_size);

		for (const auto & [tid, addresses] : pid_pages_map) {
			std::vector<addr_t> pages(addresses.begin(), addresses.end());

			const auto nodes = memory_info::get_pages_current_node(pages, tid);

			// update map
			std::transform(pages.begin(), pages.end(), nodes.begin(), std::inserter(page_node_map, page_node_map.end()),
			               std::make_pair<const addr_t &, const node_t &>);
		}

		last_map_size = page_node_map.size();

		return page_node_map;
	}

	static void process_samples(const std::vector<samples::pebs> & samples) {
		size_t total_inst  = 0;
		size_t total_flops = 0;
		size_t total_reqs  = 0;
		size_t total_mem   = 0;

		size_t discarded       = 0;
		size_t discarded_inst  = 0;
		size_t discarded_flops = 0;
		size_t discarded_reqs  = 0;
		size_t discarded_mem   = 0;

		// Map storing page -> node correspondence
		auto page_node_map = pages_node_map(samples);

		// Pages with non-valid information (probably kernel pages)
		uset<addr_t> discarded_pages;

		const auto secs_since_last_memory_mig = utils::time::time_until_now(memory::last_mig_time);
		const auto secs_to_next_mig = std::max(memory::min_time_between_migrations - secs_since_last_memory_mig, {});
		const auto ageing_factor    = real_t(1.0) / (1 + secs_to_next_mig);

		for (const auto & sample : samples) {
			switch (sample.type()) {
				case samples::MEM_SAMPLE:
					++total_mem;
					if (!process_memory_sample(sample, ageing_factor, page_node_map)) {
						++discarded;
						++discarded_mem;
						if (verbose::print_with_lvl(verbose::LVL_MAX)) {
							discarded_pages.insert(memory_info::page_from_addr(sample.sample_addr));
						}
					}
					break;
				case samples::REQ_SAMPLE:
					++total_reqs;
					if (!process_req_sample(sample)) {
						++discarded;
						++discarded_reqs;
					}
					break;
				case samples::INS_SAMPLE:
					++total_inst;
					if (!process_inst_sample(sample)) {
						++discarded;
						++discarded_inst;
					}
					break;
				default:
					// Treat this sample as if it counts Vector (SIMD) floating-point operations
					++total_flops;
					if (!process_flops_sample(sample)) {
						++discarded;
						++discarded_flops;
					}
					break;
			}
		}

		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << "Processed " << samples.size() << " samples. " << discarded << " discarded ("
			          << utils::string::percentage(discarded, samples.size()) << "%):" << '\n';
			std::cout << '\t' << total_inst << " INST samples processed. " << discarded_inst << " discarded ("
			          << utils::string::percentage(discarded_inst, total_inst) << "%)" << '\n';
#ifndef JUST_INS
			std::cout << '\t' << total_flops << " FLOPS samples processed. " << discarded_flops << " discarded ("
			          << utils::string::percentage(discarded_flops, total_flops) << "%)" << '\n';
#endif
			std::cout << '\t' << total_reqs << " REQS samples processed. " << discarded_reqs << " discarded ("
			          << utils::string::percentage(discarded_reqs, total_reqs) << "%)" << '\n';
			std::cout << '\t' << total_mem << " MEM samples processed. " << discarded_mem << " discarded ("
			          << utils::string::percentage(discarded_mem, total_mem) << "%)" << '\n';

			if (verbose::print_with_lvl(verbose::LVL_MAX)) {
				std::cout << "Discarded memory pages:" << '\n';
				for (const auto & page : discarded_pages) {
					std::cout << utils::string::to_string_hex(page) << '\n';
				}
			}
		}
	}

	inline auto change_thread_strategy(const thread::strategy_t new_strat) -> bool {
		thread::strategy = new_strat;
		return true;
	}

	inline auto change_memory_strategy(const memory::strategy_t new_strat) -> bool {
		memory::strategy = new_strat;
		return true;
	}

	inline void print_thread_info_header(std::ofstream & thread_file) {
		thread_file << "Timestamp" << ';';
		thread_file << "TID" << ';' << "PID" << ';' << "CMDLINE" << ';' << "State" << ';';
		thread_file << "CPU" << ';' << "Node" << ';' << "PrefNode" << ';' << "InPrefNode" << ';';
		thread_file << "Perf" << ';' << "CPU%" << ';' << "RelPerf" << ';';
		thread_file << "Ops" << ';' << "OpIntensity" << ';' << "AvLat" << '\n';
	}

	inline void print_memory_info_header(std::ofstream & memory_file) {
		memory_file << "Timestamp" << ';' << "Address" << ';' << "Node" << ';' << "PrefNode" << ';' << "InPrefNode"
		            << ';';
		for (const auto & node : system_info::nodes()) {
			memory_file << "ReqsNode" << utils::string::to_string(node, 0) << ';';
		}
		for (const auto & node : system_info::nodes()) {
			memory_file << "AgedReqsNode" << utils::string::to_string(node, 0) << ';';
		}
		for (const auto & node : system_info::nodes()) {
			memory_file << "RatioNode" << utils::string::to_string(node, 0) << ';';
		}
		for (const auto & node : system_info::nodes()) {
			memory_file << "AvLatencyNode" << utils::string::to_string(node, 0) << ';';
		}
		memory_file << "AvLatency" << ';';
		memory_file << "Samples" << '\n';
	}

	template<class T>
	static void print_thread_info(const T timestamp, std::ofstream & thread_file) {
		// Create a temporal copy of performance table
		auto aux_perf_table = migration::thread::perf_table;

		// Update performance table
		aux_perf_table.update();

		for (const auto & [tid, info] : aux_perf_table) {
			if (!system_info::is_pid_alive(tid)) { continue; }

			const auto cpu       = system_info::cpu_from_tid(tid);
			const auto node      = system_info::node_from_cpu(cpu);
			const auto cmdline   = system_info::cmdline(tid);
			const auto pref_node = info.preferred_node();

			thread_file << utils::string::to_string(timestamp, 0) << ';';
			thread_file << utils::string::to_string(tid, 0) << ';';
			thread_file << utils::string::to_string(system_info::pid_from_tid(tid), 0) << ';';
			thread_file << (cmdline.empty() ? "UNKNOWN" : cmdline) << ';';
			thread_file << system_info::state(tid) << ';';
			thread_file << utils::string::to_string(cpu, 0) << ';';
			thread_file << utils::string::to_string(node, 0) << ';';
			thread_file << utils::string::to_string(pref_node, 0) << ';';
			thread_file << utils::string::to_string((node == pref_node ? 1 : 0), 0) << ';';
			thread_file << utils::string::to_string(aux_perf_table.performance(tid), 0) << ';';
			thread_file << utils::string::percentage(system_info::cpu_use(tid), 1, 0) << ';';
			thread_file << utils::string::percentage(aux_perf_table.rel_performance(tid), 1, 2) << ';';
			thread_file << utils::string::to_string(info.ops_per_s(node), 0) << ';';
			thread_file << utils::string::to_string(info.ops_per_byte(node), 2) << ';';
			thread_file << utils::string::to_string(info.av_latency(node), 0);
			thread_file << '\n';
		}
	}

	template<class T>
	static void print_memory_info(const T timestamp, std::ofstream & memory_file) {
		auto aux_perf_table = migration::memory::perf_table;

		for (const auto & [addr, info] : aux_perf_table) {
			const auto node      = aux_perf_table.node(addr);
			const auto pref_node = info.preferred_node();

			memory_file << utils::string::to_string(timestamp, 0) << ';';
			memory_file << utils::string::to_string_hex(addr) << ';';
			memory_file << utils::string::to_string(node, 0) << ';';
			memory_file << utils::string::to_string(pref_node, 0) << ';';
			memory_file << (node == pref_node ? 1 : 0) << ';';
			for (const auto & reqs : info.raw_accesses()) {
				memory_file << utils::string::to_string(reqs, 0) << ';';
			}
			for (const auto & reqs : info.node_accesses()) {
				memory_file << utils::string::to_string(reqs, 2) << ';';
			}
			for (const auto & ratio : info.ratios()) {
				memory_file << utils::string::to_string(ratio, 2) << ';';
			}
			for (const auto & lat : info.av_latencies()) {
				memory_file << utils::string::to_string(lat, 0) << ';';
			}
			memory_file << utils::string::to_string(info.av_latency(), 0) << ';';
			memory_file << utils::string::to_string(info.samples_count(), 0) << '\n';
		}
	}

	static auto balance(const bool ignore_idle = true) -> bool {
		// Balance CPUs workload
		const auto strat = thread::current_strategy();

		const auto migrations = strat->balance(ignore_idle);

		if (!migrations.empty()) {
			thread::perf_table.update();

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "CPU load balance was required and performed" << '\n';
				std::cout << "Performance table: " << '\n' << migration::thread::perf_table << '\n';
			}
		} else if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << "CPU load was balanced and no action was needed" << '\n';
		}

		return migrations.empty();
	}

	inline void remove_invalid_pids(const set<pid_t> & pids) {
		for (const auto & pid : pids) {
			thread::perf_table.remove_entry(pid);
		}
	}

	inline void add_pids(const set<pid_t> & pids) {
		thread::perf_table.add_tids(pids);
	}

	inline void add_pids() {
		thread::perf_table.add_tids(system_info::get_children());
	}

	inline void migrate(const time_point & current_time = hres_clock::now()) {
		const auto secs_since_last_thread_mig = utils::time::time_until(thread::last_mig_time, current_time);
		const auto secs_since_last_memory_mig = utils::time::time_until(memory::last_mig_time, current_time);

		const bool perform_thread_mig = secs_since_last_thread_mig > thread::get_time_value();
		const bool perform_memory_mig = secs_since_last_memory_mig > memory::min_time_between_migrations;

		if (perform_thread_mig) { thread::perform_strategy(current_time); }

		if (perform_memory_mig) { memory::perform_strategy(current_time); }
	}
} // namespace migration

#endif /* end of include guard: THANOS_MIGRATION_HPP */
