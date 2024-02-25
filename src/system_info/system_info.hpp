/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_SYSTEM_INFO_HPP
#define THANOS_SYSTEM_INFO_HPP

#include <ext/alloc_traits.h> // for __alloc_traits<>::value_type
#include <features.h>         // for __glibc_unlikely
#include <numa.h>             // for numa_distance, numa_max_node
#include <sys/sysinfo.h>      // for get_nprocs
#include <unistd.h>           // for pid_t, getppid, size_t

#include <filesystem>  // for path
#include <iostream>    // for operator<<, char_traits, basic...
#include <map>         // for operator==, _Rb_tree_const_ite...
#include <memory>      // for allocator_traits<>::value_type
#include <numeric>     // for accumulate
#include <ranges>      // for rages::iota_view
#include <set>         // for set, set<>::const_iterator
#include <string>      // for string, to_string, operator+
#include <string_view> // for string_view
#include <variant>     // for variant
#include <vector>      // for vector, allocator

#include "processes/process.hpp"      // for process, process::DEFAULT_PROC
#include "processes/process_tree.hpp" // for process_tree, operator<<
#include "tabulate/tabulate.hpp"      // for Table, Format
#include "utils/string.hpp"           // for to_string
#include "utils/types.hpp"            // for node_t, cpu_t, real_t

namespace system_info {
	static constexpr real_t IDLE_THRESHOLD = 0.01;

	[[nodiscard]] inline auto local_distance() {
		static const auto distance = numa_distance(0, 0);
		return distance;
	}

	namespace details {
		extern std::vector<node_t> nodes;
		extern std::vector<cpu_t>  cpus;

		extern std::vector<std::vector<node_t>> nodes_by_distance; // Contains the list of nodes sorted by distance
		// E.g. nodes_by_distance[1] = {1, 0, 2, 3} -> list of nodes, sorted by NUMA distance from node 1,
		// so 1 is the "closest" node (obviously), 0 is the closest neighbour, and 3 is the furthest neighbour

		// To know where each CPU is (in terms of memory node)
		extern std::vector<node_t>             cpu_node_map; // input: CPU,  output: node
		extern std::vector<std::vector<cpu_t>> node_cpu_map; // input: node, output: list of CPUs

		// To know where each TID is (in terms of CPUs and node)
		extern std::vector<set<pid_t>> cpu_tid_map;  // input: CPU,  output: list of TIDs
		extern std::vector<set<pid_t>> node_tid_map; // input: node, output: list of TIDs

		extern process_tree proc_tree; // processes tree

		extern long int default_priority;
	} // namespace details

	namespace auxiliary_functions {
		// CPU-pin/free methods
		inline auto pin_thread_to_cpu(const pid_t tid, const cpu_t cpu, const bool print = true) -> bool {
			if (__glibc_unlikely(std::cmp_equal(tid, 0))) {
				std::cerr << "Error: TID is 0. Omitting sched_setaffinity() call..." << '\n';
				return false;
			}

			process & proc = details::proc_tree.retrieve(tid);

			cpu_t old_cpu = proc.pinned_cpu();

			node_t old_node = details::cpu_node_map[old_cpu];
			node_t new_node = details::cpu_node_map[cpu];

			if (proc.pin(cpu, print)) {
				if (std::cmp_not_equal(old_cpu, proc.pinned_cpu())) {
					details::cpu_tid_map[old_cpu].erase(tid);
					details::cpu_tid_map[cpu].insert(tid);

					if (std::cmp_not_equal(old_node, new_node)) {
						details::node_tid_map[old_node].erase(tid);
						details::node_tid_map[new_node].erase(tid);
					}
				}
				return true;
			}

			return false;
		}

		inline auto pin_thread_to_cpu(process & proc, const bool print = true) -> bool {
			if (proc.pin(print)) {
				details::cpu_tid_map[proc.pinned_cpu()].insert(proc.pid());
				details::node_tid_map[proc.pinned_node()].insert(proc.pid());
				return true;
			}
			return false;
		}

		// Node-pin/free methods
		inline auto pin_thread_to_node(const pid_t tid, const node_t node, const bool print = true) -> bool {
			if (__glibc_unlikely(std::cmp_equal(tid, 0))) {
				std::cerr << "Error: TID is 0. Omitting sched_setaffinity() call..." << '\n';
				return false;
			}

			process & proc = details::proc_tree.retrieve(tid);

			const auto old_cpu = proc.pinned_cpu();

			const auto old_node = details::cpu_node_map[old_cpu];

			if (proc.pin_node(node, print)) {
				if (std::cmp_not_equal(old_cpu, proc.pinned_cpu())) {
					details::cpu_tid_map[old_cpu].erase(tid);
					details::cpu_tid_map[proc.pinned_cpu()].insert(tid);

					if (std::cmp_not_equal(old_node, node)) {
						details::node_tid_map[old_node].erase(tid);
						details::node_tid_map[node].erase(tid);
					}
				}
				return true;
			}

			return false;
		}

		inline auto pin_thread_to_node(process & proc, const bool print = true) -> bool {
			if (proc.pin_node(print)) {
				const auto node = details::cpu_node_map[proc.pinned_cpu()];
				details::node_tid_map[node].insert(proc.pid());
				return true;
			}
			return false;
		}

		inline auto unpin_thread(process & proc, const bool print = true) -> bool {
			const auto pid      = proc.pid();
			const auto old_cpu  = proc.pinned_cpu();
			const auto old_node = proc.pinned_node();

			if (proc.unpin(print)) {
				details::cpu_tid_map[old_cpu].erase(pid);
				details::node_tid_map[old_node].erase(pid);
				return true;
			}

			return false;
		}

		inline auto unpin_thread(const pid_t tid, const bool print = true) -> bool {
			auto & proc = details::proc_tree.retrieve(tid);

			return unpin_thread(proc, print);
		}

		inline auto priority_to_weight(const long int priority = 20) {
			static constexpr std::array<int, 40> sched_prio_to_weight = {
				/* -20 */ 88761, 71755, 56483, 46273, 36291,
				/* -15 */ 29154, 23254, 18705, 14949, 11916,
				/* -10 */ 9548,  7620,  6100,  4904,  3906,
				/*  -5 */ 3121,  2501,  1991,  1586,  1277,
				/*   0 */ 1024,  820,   655,   526,   423,
				/*   5 */ 335,   272,   215,   172,   137,
				/*  10 */ 110,   87,    70,    56,    45,
				/*  15 */ 36,    29,    23,    18,    15,
			};

			return sched_prio_to_weight.at(priority);
		}
	} // namespace auxiliary_functions

	auto detect_system() noexcept -> bool;

	[[nodiscard]] inline auto num_of_cpus() {
		return details::cpus.size();
	}

	[[nodiscard]] inline auto num_of_nodes() {
		return details::nodes.size();
	}

	[[nodiscard]] inline auto max_node() {
		static const auto MAX_NODE = numa_max_node();
		return MAX_NODE;
	}

	[[nodiscard]] inline auto max_cpu() {
		static const auto MAX_CPU = *std::ranges::max_element(details::cpus);
		return MAX_CPU;
	}

	[[nodiscard]] inline auto cpus_per_node() {
		static const auto N_CPUS = num_of_cpus() / num_of_nodes();
		return N_CPUS;
	}

	// Functions to scan files in /proc to search for children threads and processes
	void scan_dir(const std::filesystem::path & dir, process * parent = nullptr);

	void scan_children_file(const std::filesystem::path & path, process * parent);

	// Scans full /proc directory. As almost all processes are not migratable, is not efficient.
	inline void update_tree(const bool scan_proc) {
		if (scan_proc) { scan_dir(process::DEFAULT_PROC); }
		details::proc_tree.update();
	}

	// Parses /proc directory from the requested pid and its children. Supposed to be efficient.
	inline void update_tree(const pid_t pid, const std::string_view dirname = process::DEFAULT_PROC) {
		process * proc = &details::proc_tree.retrieve(pid);

		const std::string subdir = std::string(dirname) + "/" + std::to_string(pid) + "/task";

		// Scan /proc directory to find new child PIDs
		scan_dir(subdir, proc);

		// Update inner information of PIDs in the process tree
		details::proc_tree.update();
	}

	// Initialise the tree of processes
	inline void start_tree(const pid_t root_pid, const std::string_view dirname = process::DEFAULT_PROC) {
		details::proc_tree = process_tree(root_pid);
		update_tree(root_pid, dirname);
	}

	[[nodiscard]] inline auto num_of_cpus(const node_t node) {
		return details::node_cpu_map[node].size();
	}

	[[nodiscard]] inline auto cpus() -> const auto & {
		return details::cpus;
	}

	[[nodiscard]] inline auto nodes() -> const auto & {
		return details::nodes;
	}

	[[nodiscard]] inline auto nodes_by_distance(const node_t node) -> const auto & {
		return details::nodes_by_distance[node];
	}

	[[nodiscard]] inline auto cpus_from_node(const node_t node) -> const auto & {
		return details::node_cpu_map[node];
	}

	[[nodiscard]] inline auto node_from_cpu(const cpu_t cpu) -> node_t {
		return details::cpu_node_map[cpu];
	}

	[[nodiscard]] inline auto pinned_cpu_from_tid(const pid_t tid) -> cpu_t {
		return details::proc_tree.retrieve(tid).pinned_cpu();
	}

	[[nodiscard]] inline auto cpu_from_tid(const pid_t tid) -> cpu_t {
		return details::proc_tree.retrieve(tid).cpu();
	}

	[[nodiscard]] inline auto pinned_node_from_tid(const pid_t tid) -> node_t {
		return details::proc_tree.retrieve(tid).pinned_node();
	}

	[[nodiscard]] inline auto node_from_tid(const pid_t tid) -> node_t {
		return details::proc_tree.retrieve(tid).node();
	}

	[[nodiscard]] inline auto ith_cpu_from_node(const node_t node, const size_t i) -> cpu_t {
		return details::node_cpu_map[node][i];
	}

	[[nodiscard]] inline auto is_cpu_free(const cpu_t cpu) -> bool {
		return details::cpu_tid_map[cpu].empty();
	}

	[[nodiscard]] inline auto is_cpu_free_in_node(const node_t node) -> bool {
		return details::node_tid_map[node].size() < details::node_cpu_map[node].size();
	}

	[[nodiscard]] inline auto tids_from_cpu(const cpu_t cpu) -> const auto & {
		return details::cpu_tid_map[cpu];
	}

	[[nodiscard]] inline auto tids_from_node(const node_t node) -> const auto & {
		return details::node_tid_map[node];
	}

	// Check if PID is a Lightweight process, such as an OMP thread
	[[nodiscard]] inline auto pid_is_lwp(const pid_t pid) -> bool {
		return details::proc_tree.retrieve(pid).lwp();
	}

	[[nodiscard]] inline auto pid_from_tid(const cpu_t tid) -> pid_t {
		// If the TID is the same as the proc root, there is no parent to retrieve
		if (std::cmp_equal(tid, details::proc_tree.root())) { return tid; }

		// "Lightweight" threads' PPID refer to their parent's PPID
		// so, take parent's PID if it exists, if not, return PPID
		const auto & tid_struct = details::proc_tree.retrieve(tid);

		const auto * const parent = tid_struct.parent();

		return tid_struct.lwp() ? parent->pid() : tid_struct.ppid();
	}

	[[nodiscard]] inline auto set_tid_cpu(const pid_t tid, const cpu_t cpu) -> bool {
		return auxiliary_functions::pin_thread_to_cpu(tid, cpu);
	}

	[[nodiscard]] inline auto set_tid_node(const pid_t tid, const node_t node) -> bool {
		return auxiliary_functions::pin_thread_to_node(tid, node);
	}

	[[nodiscard]] inline auto is_idle(const process & process) -> bool {
		return process.cpu_use() < IDLE_THRESHOLD;
	}

	[[nodiscard]] inline auto is_idle(const pid_t tid) -> bool {
		return is_idle(details::proc_tree.retrieve(tid));
	}

	[[nodiscard]] inline auto is_pinned(const pid_t tid) -> bool {
		return details::proc_tree.retrieve(tid).is_pinned();
	}

	[[nodiscard]] inline auto is_running(const pid_t tid) -> bool {
		return details::proc_tree.retrieve(tid).is_running();
	}

	[[nodiscard]] inline auto is_migratable(const pid_t tid) -> bool {
		return details::proc_tree.retrieve(tid).migratable();
	}

	[[nodiscard]] inline auto is_pid_alive(const pid_t pid) -> bool {
		return details::proc_tree.is_alive(pid);
	}

	[[nodiscard]] inline auto cpu_use(const pid_t pid) -> real_t {
		return details::proc_tree.retrieve(pid).cpu_use();
	}

	[[nodiscard]] inline auto state(const pid_t pid) -> char {
		return details::proc_tree.retrieve(pid).state();
	}

	[[nodiscard]] inline auto cmdline(const pid_t pid) -> const std::string & {
		const auto & process = details::proc_tree.retrieve(pid);
		return process.cmdline();
	}

	[[nodiscard]] inline auto non_idle_tids_from_cpu(const cpu_t cpu) -> set<pid_t> {
		const auto & tids = details::cpu_tid_map[cpu];

		set<pid_t> non_idle_tids;

		for (const auto & tid : tids) {
			if (!is_idle(tid)) { non_idle_tids.insert(tid); }
		}

		return non_idle_tids;
	}

	[[nodiscard]] inline auto non_idle_tids_from_node(const node_t node) -> set<pid_t> {
		const auto & tids = details::node_tid_map[node];

		set<pid_t> non_idle_tids;

		for (const auto & tid : tids) {
			if (!is_idle(tid)) { non_idle_tids.insert(tid); }
		}

		return non_idle_tids;
	}

	[[nodiscard]] inline auto tids_from_cpu(const cpu_t cpu, const bool ignore_idle) {
		return ignore_idle ? non_idle_tids_from_cpu(cpu) : tids_from_cpu(cpu);
	}

	[[nodiscard]] inline auto tids_from_node(const node_t node, const bool ignore_idle) {
		return ignore_idle ? non_idle_tids_from_node(node) : tids_from_node(node);
	}

	[[nodiscard]] inline auto non_idle_tids() -> set<pid_t> {
		set<pid_t> tids;

		for (const auto & node : system_info::nodes()) {
			const auto node_tids = non_idle_tids_from_node(node);
			tids.insert(node_tids.begin(), node_tids.end());
		}

		return tids;
	}

	[[nodiscard]] inline auto tids() -> set<pid_t> {
		set<pid_t> tids;

		for (const auto & node : system_info::nodes()) {
			const auto node_tids = tids_from_node(node);
			tids.insert(node_tids.begin(), node_tids.end());
		}

		return tids;
	}

	[[nodiscard]] inline auto tids(const bool ignore_idle) -> set<pid_t> {
		return ignore_idle ? non_idle_tids() : tids();
	}

	[[nodiscard]] inline auto pin_all_threads() -> bool {
		bool success = true;

		process & root_proc = details::proc_tree.retrieve();

		success &= auxiliary_functions::pin_thread_to_cpu(root_proc);

		auto children = root_proc.all_children();

		for (auto & child : children) {
			success &= auxiliary_functions::pin_thread_to_cpu(*child);
		}

		return success;
	}

	[[nodiscard]] inline auto pin_all_threads_node() -> bool {
		bool success = true;

		process & root_proc = details::proc_tree.retrieve();

		success &= auxiliary_functions::pin_thread_to_node(root_proc);

		auto children = root_proc.all_children();

		for (auto & child : children) {
			success &= auxiliary_functions::pin_thread_to_node(*child);
		}

		return success;
	}

	[[nodiscard]] inline auto pin_non_idle_threads() -> bool {
		bool success = true;

		process & root_proc = details::proc_tree.retrieve();

		success &= auxiliary_functions::pin_thread_to_cpu(root_proc);

		auto children = root_proc.all_children();

		for (auto & child : children) {
			if (is_idle(*child)) {
				success &= auxiliary_functions::unpin_thread(*child);
			} else {
				success &= auxiliary_functions::pin_thread_to_cpu(*child);
			}
		}

		return success;
	}

	[[nodiscard]] inline auto pin_non_idle_threads_node() -> bool {
		bool success = true;

		process & root_proc = details::proc_tree.retrieve();

		success &= auxiliary_functions::pin_thread_to_node(root_proc);

		auto children = root_proc.all_children();

		for (auto & child : children) {
			if (is_idle(*child)) {
				success &= auxiliary_functions::unpin_thread(*child);
			} else {
				success &= auxiliary_functions::pin_thread_to_node(*child);
			}
		}

		return success;
	}

	[[nodiscard]] inline auto pin_threads_cpu(const bool ignore_idle) -> bool {
		return ignore_idle ? pin_non_idle_threads() : pin_all_threads();
	}

	[[nodiscard]] inline auto pin_threads_node(const bool ignore_idle) -> bool {
		return ignore_idle ? pin_non_idle_threads_node() : pin_all_threads_node();
	}

	[[nodiscard]] inline auto unpin_all_threads(const bool print = true) -> bool {
		bool success = true;

		process & root_proc = details::proc_tree.retrieve();

		auto children = root_proc.all_children();

		for (auto & child : children) {
			success &= child->unpin(print);
		}

		return success;
	}

	inline void remove_pid(const pid_t tid, const bool print = true) {
		process & proc = details::proc_tree.retrieve(tid);
		details::cpu_tid_map[proc.cpu()].erase(tid);

		auxiliary_functions::unpin_thread(tid, print);
	}

	inline void remove_invalid_data(const set<pid_t> & invalid_pids) {
		for (const auto pid : invalid_pids) {
			for (auto & tids : details::cpu_tid_map) {
				tids.erase(pid);
			}
			for (auto & tids : details::node_tid_map) {
				tids.erase(pid);
			}
		}
	}

	[[nodiscard]] inline auto remove_invalid_data() {
		auto invalid_pids = details::proc_tree.erase_invalid();

		remove_invalid_data(invalid_pids);

		return invalid_pids;
	}

	[[nodiscard]] inline auto get_children() -> set<pid_t> {
		const auto & process       = details::proc_tree.retrieve();
		const auto   proc_children = process.all_children();

		set<pid_t> children;
		children.insert(process.pid());
		for (const auto & child_ptr : proc_children) {
			children.insert(child_ptr->pid());
		}

		return children;
	}

	[[nodiscard]] inline auto get_children(const pid_t pid) -> set<pid_t> {
		const auto & process       = details::proc_tree.retrieve(pid);
		const auto   proc_children = process.all_children();

		set<pid_t> children;
		for (const auto & child_ptr : proc_children) {
			children.insert(child_ptr->pid());
		}

		return children;
	}

	[[nodiscard]] inline auto get_lwp_children(const pid_t pid) -> set<pid_t> {
		const auto & process       = details::proc_tree.retrieve(pid);
		const auto   proc_children = process.all_children();

		set<pid_t> children;
		for (const auto & child_ptr : proc_children) {
			if (child_ptr->lwp()) { children.insert(child_ptr->pid()); }
		}

		return children;
	}

	inline auto update(const pid_t child_process) -> set<pid_t> {
		// Save the current list of children
		const auto last_children = get_children(child_process);
		// Look for new child threads of the children process and update the information
		update_tree(child_process);
		// Get the updated list of PIDs of the children
		const auto children = get_children(child_process);

		set<pid_t> threads_to_remove;

		for (const auto & pid : last_children) {
			if (!children.contains(pid)) { threads_to_remove.insert(pid); }
		}

		remove_invalid_data(threads_to_remove);

		return threads_to_remove;
	}

	inline void end() {
		std::ignore = unpin_all_threads(false); // do no print verbose messages
		details::proc_tree.erase_invalid();
	}

	[[nodiscard]] inline auto memory_usage(const pid_t pid) {
		static constexpr auto MB_to_B = 1024 * 1024;

		std::vector<real_t> mem_usage(max_node() + 1, {});

		// Get amount of memory allocated (in MBs)
		const auto command = "(NUMASTAT_WIDTH=1000 numastat -p " + std::to_string(pid) + " 2> /dev/null) " +
		                     " | tail -n 1 | grep -P -o '[0-9]+.[0-9]+'";
		const auto output = utils::cmd::exec(command, false);

		std::istringstream is(output);

		for (auto & mem : mem_usage) {
			real_t amount = {};
			is >> amount;

			mem = amount * MB_to_B;
		}

		return mem_usage;
	}

	[[nodiscard]] inline auto memory_usage(const pid_t pid, const node_t node) {
		const auto mem_usage = memory_usage(pid);
		return mem_usage[node];
	}

	// Formula extracted from "Optimizing Google’s Warehouse Scale Computers: The NUMA Experience"
	// https://ieeexplore.ieee.org/abstract/document/6522318
	[[nodiscard]] inline auto numa_score(const pid_t pid) -> real_t {
		// Get memory usage from a process (system shows its LWP children also)
		auto mem_usage = memory_usage(pid);

		// Get CPU usage from the process and its LWP children)
		std::vector<real_t> cpu_usage_by_node(max_node() + 1, {});
		// Self process...
		cpu_usage_by_node[pinned_node_from_tid(pid)] += cpu_use(pid);
		// ... and LWP
		for (const auto child : get_lwp_children(pid)) {
			const auto node = pinned_node_from_tid(child);
			cpu_usage_by_node[node] += cpu_use(child);
		}

		// Normalise usages
		const auto total_cpu_usage = std::reduce(cpu_usage_by_node.begin(), cpu_usage_by_node.end());
		const auto total_mem_usage = std::reduce(mem_usage.begin(), mem_usage.end());

		for (const auto node : nodes()) {
			cpu_usage_by_node[node] /= std::isnormal(total_cpu_usage) ? total_cpu_usage : real_t{ 1 };
			mem_usage[node] /= std::isnormal(total_mem_usage) ? total_mem_usage : real_t{ 1 };
		}

		// Compute score
		real_t score = {};

		for (const auto i : nodes()) {
			for (const auto j : nodes()) {
				score += cpu_usage_by_node[i] * cpu_usage_by_node[j] * static_cast<real_t>(local_distance()) /
				         static_cast<real_t>(numa_distance(i, j));
			}
		}

		return score;
	}

	[[nodiscard]] inline auto overload_cpu() {
		return 1.0;
	}

	[[nodiscard]] inline auto overload_node(const node_t node) {
		return overload_cpu() * static_cast<real_t>(cpus_from_node(node).size());
	}

	[[nodiscard]] inline auto overload_system() {
		return overload_cpu() * static_cast<real_t>(num_of_cpus());
	}

	[[nodiscard]] inline auto load_per_pid() -> map<pid_t, real_t> {
		const auto processes_ptr = details::proc_tree.retrieve_all();

		std::vector<size_t> cpu_processes(num_of_cpus());

		for (const auto * process_ptr : processes_ptr) {
			++cpu_processes[process_ptr->cpu()];
		}

		map<pid_t, real_t> pid_load_map;

		for (const auto * process_ptr : processes_ptr) {
			const auto cpu          = process_ptr->is_pinned() ? process_ptr->pinned_cpu() : process_ptr->cpu();
			const auto max_cpu_use  = real_t(1.0) / static_cast<real_t>(cpu_processes[cpu]);
			const auto real_cpu_use = process_ptr->cpu_use() / max_cpu_use;
			const auto weight       = auxiliary_functions::priority_to_weight(process_ptr->priority());

			pid_load_map[process_ptr->pid()] = static_cast<real_t>(weight) * real_cpu_use /
			                                   static_cast<real_t>(auxiliary_functions::priority_to_weight());
		}

		return pid_load_map;
	}

	[[nodiscard]] inline auto load_per_cpu(const map<pid_t, real_t> & pid_load_map, const bool only_pinned = false)
	    -> std::vector<real_t> {
		std::vector<real_t> cpu_load_map(num_of_cpus(), {});

		for (const auto & [pid, load] : pid_load_map) {
			const auto & process = details::proc_tree.retrieve(pid);
			if ((!only_pinned || process.is_pinned())) { cpu_load_map[process.pinned_cpu()] += load; }
		}

		return cpu_load_map;
	}

	[[nodiscard]] inline auto load_per_cpu(const bool only_pinned = false) -> std::vector<real_t> {
		return load_per_cpu(load_per_pid(), only_pinned);
	}

	[[nodiscard]] inline auto load_per_node(const map<pid_t, real_t> & pid_load_map, const bool only_pinned = false)
	    -> std::vector<real_t> {
		std::vector<real_t> node_load_map(max_node() + 1, {});

		for (const auto & [pid, load] : pid_load_map) {
			const auto & process = details::proc_tree.retrieve(pid);
			if ((!only_pinned || process.is_pinned())) { node_load_map[process.pinned_node()] += load; }
		}

		return node_load_map;
	}

	[[nodiscard]] inline auto load_per_node(const bool only_pinned = false) -> std::vector<real_t> {
		return load_per_node(load_per_pid(), only_pinned);
	}

	[[nodiscard]] inline auto load_cpu(const map<pid_t, real_t> & pid_load_map, const cpu_t cpu,
	                                   const bool only_pinned = false) {
		return std::accumulate(pid_load_map.begin(), pid_load_map.end(), real_t{},
		                       [&](const auto & acc, const auto & pid_load_pair) {
			                       const auto & [pid, load] = pid_load_pair;
			                       const auto & process     = details::proc_tree.retrieve(pid);
			                       if ((!only_pinned || process.is_pinned()) && process.pinned_cpu() == cpu) {
				                       return acc + load;
			                       }
			                       return acc;
		                       });
	}

	[[nodiscard]] inline auto load_cpu(const cpu_t cpu, const bool only_pinned = false) {
		return load_cpu(load_per_pid(), cpu, only_pinned);
	}

	[[nodiscard]] inline auto load_node(const map<pid_t, real_t> & pid_load_map, const node_t node,
	                                    const bool only_pinned = false) {
		return std::accumulate(pid_load_map.begin(), pid_load_map.end(), real_t{},
		                       [&](const auto & acc, const auto & pid_load_pair) {
			                       const auto & [pid, load] = pid_load_pair;
			                       const auto & process     = details::proc_tree.retrieve(pid);
			                       if ((!only_pinned || process.is_pinned()) && process.pinned_node() == node) {
				                       return acc + load;
			                       }
			                       return acc;
		                       });
	}

	[[nodiscard]] inline auto load_node(const node_t node, const bool only_pinned = false) {
		return load_node(load_per_pid(), node, only_pinned);
	}

	[[nodiscard]] inline auto load_system(const map<pid_t, real_t> & pid_load_map, const bool only_pinned = false) {
		return std::accumulate(pid_load_map.begin(), pid_load_map.end(), real_t{},
		                       [&](const auto & acc, const auto & pid_load_pair) {
			                       const auto & [pid, load] = pid_load_pair;
			                       const auto & process     = details::proc_tree.retrieve(pid);
			                       if (!only_pinned || process.is_pinned()) { return acc + load; }
			                       return acc;
		                       });
	}

	[[nodiscard]] inline auto load_system(const bool only_pinned = false) {
		return load_system(load_per_pid(), only_pinned);
	}

	inline void print_system_topology(std::ostream & os = std::cout) {
		os << "Detected system: " << num_of_cpus() << " total CPUs, " << num_of_nodes() << " memory nodes, "
		   << cpus_per_node() << " CPUs per node." << '\n';

		os << '\n';

		// Print distance table
		os << "Nodes distance matrix:" << '\n';

		tabulate::Table distance_table;

		std::vector<std::string> nodes_str;
		nodes_str.reserve(nodes().size() + 1);

		nodes_str.emplace_back("");

		for (const auto & node : nodes()) {
			nodes_str.emplace_back("Node " + std::to_string(node));
		}

		distance_table.add_row({ nodes_str.begin(), nodes_str.end() });

		for (const auto & n1 : nodes()) {
			std::vector<std::string> distances_str = {};
			distances_str.reserve(nodes().size() + 1);

			distances_str.emplace_back("Node " + std::to_string(n1));

			for (const auto & n2 : nodes()) {
				distances_str.emplace_back(std::to_string(numa_distance(n1, n2)));
			}

			distance_table.add_row({ distances_str.begin(), distances_str.end() });
		}
		distance_table.format().hide_border();
		distance_table.print(os);

		os << '\n';

		// Print CPUs
		os << "NUMA node - CPU map: " << '\n';

		tabulate::Table node_cpus_table;

		node_cpus_table.add_row({ "Node", "CPUs" });

		for (const auto & node : system_info::nodes()) {
			std::string cpus_str;

			for (const auto & cpu : cpus_from_node(node)) {
				cpus_str += std::to_string(cpu) + " ";
			}

			node_cpus_table.add_row({ std::to_string(node), cpus_str });
		}

		node_cpus_table.format().hide_border();
		node_cpus_table.print(os);

		os << '\n';
	}

	inline void print_system_status(std::ostream & os = std::cout) {
		details::proc_tree.retrieve().update_all();

		// Print processes tree
		os << "Processes tree..." << '\n';
		os << details::proc_tree << '\n';

		// Print CPU - TID map
		std::vector<std::set<pid_t>> cpu_tid_set(num_of_cpus(), std::set<pid_t>());
		std::vector<real_t>          cpu_use(num_of_cpus(), real_t());
		std::vector<real_t>          node_use(num_of_nodes(), real_t());

		real_t system_use{};

		const auto children = details::proc_tree.retrieve_all();

		for (const auto & process_ptr : children) {
			const auto pid  = process_ptr->pid();
			const auto cpu  = process_ptr->cpu();
			const auto node = process_ptr->node();
			const auto use  = process_ptr->cpu_use();

			cpu_tid_set[cpu].insert(pid);
			cpu_use[cpu] += use;
			node_use[node] += use;
			system_use += use;
		}

		// Print CPU - TID map
		tabulate::Table cpu_tid_table;

		cpu_tid_table.add_row({ "CPU", "TIDs" });

		for (const auto & cpu : cpus()) {
			const auto & tid_set = cpu_tid_set[cpu];

			std::string tids_str;
			for (const auto & tid : tid_set) {
				tids_str += std::to_string(tid) + " ";
			}
			cpu_tid_table.add_row({ std::to_string(cpu), tids_str });
		}

		cpu_tid_table.format().hide_border();
		cpu_tid_table.print(os);
		os << '\n' << '\n';

		// Print total system load
		os << "Load avg (1m / 5m / 15m): " << utils::cmd::exec(R"(cat /proc/loadavg | awk '{print($1" / "$2" / "$3)}')")
		   << '\n';

		const auto pid_load_map  = load_per_pid();
		const auto cpu_load_map  = load_per_cpu(pid_load_map);
		const auto node_load_map = load_per_node(pid_load_map);

		os << "Instantaneous system load: " << utils::string::to_string(load_system(pid_load_map)) << '\n' << '\n';

		// Print load per node
		tabulate::Table node_use_table;

		node_use_table.add_row({ "Node", "Load", "Use (%)", "Use bar" });

		static constexpr size_t MAX_USE_LENGTH = 20;
		for (const auto node : nodes()) {
			const auto use = node_use[node] * 100 / static_cast<real_t>(cpus_from_node(node).size());
			const auto len =
			    std::clamp<size_t>(static_cast<size_t>(std::round(use * MAX_USE_LENGTH / 100)), 0, MAX_USE_LENGTH);
			std::string use_str;
			if (std::cmp_equal(len, MAX_USE_LENGTH)) {
				use_str = std::string(len, '*');
			} else if (std::cmp_equal(len, 0)) {
				use_str = std::string(MAX_USE_LENGTH, ' ');
			} else {
				use_str = std::string(len, '*') + std::string(MAX_USE_LENGTH - len, ' ');
			}
			node_use_table.add_row({ std::to_string(node), utils::string::to_string(node_load_map[node]),
			                         utils::string::to_string(use), '|' + use_str + '|' });
		}

		node_use_table.format().hide_border();
		node_use_table.print(os);
		os << '\n';

		// Print load per CPU
		tabulate::Table cpu_use_table;

		cpu_use_table.add_row({ "CPU", "Load", "Use (%)", "Use bar" });

		for (const auto cpu : cpus()) {
			const auto use = cpu_use[cpu] * 100;
			const auto len =
			    std::clamp<size_t>(static_cast<size_t>(std::round(use * MAX_USE_LENGTH / 100)), 0, MAX_USE_LENGTH);
			std::string use_str;
			if (std::cmp_equal(len, MAX_USE_LENGTH)) {
				use_str = std::string(len, '*');
			} else if (std::cmp_equal(len, 0)) {
				use_str = std::string(MAX_USE_LENGTH, ' ');
			} else {
				use_str = std::string(len, '*') + std::string(MAX_USE_LENGTH - len, ' ');
			}
			cpu_use_table.add_row({ std::to_string(cpu), utils::string::to_string(cpu_load_map[cpu]),
			                        utils::string::to_string(use), '|' + use_str + '|' });
		}

		cpu_use_table.format().hide_border();
		cpu_use_table.print(os);

		os << '\n' << '\n';
	}
} // namespace system_info

#endif /* end of include guard: THANOS_SYSTEM_INFO_HPP */
