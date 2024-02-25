/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include "system_info.hpp"

#include <filesystem>

#include <cerrno>  // for errno
#include <cstring> // for strerror

#include "types.hpp"   // for node_t, cpu_t
#include "verbose.hpp" // for DEFAULT_LVL, lvl

namespace system_info {
	namespace details {
		std::vector<node_t> nodes;
		std::vector<cpu_t>  cpus;

		std::vector<std::vector<node_t>> nodes_by_distance; // Contains the list of nodes sorted by distance
		    // E.g. nodes_by_distance[1] = {1, 0, 2, 3} -> list of nodes, sorted by NUMA distance from node 1,
		    // so 1 is the "closest" node (obviously), 0 is the closest neighbour, and 3 is the furthest neighbour

		// To know where each CPU is (in terms of memory node)
		std::vector<node_t>             cpu_node_map; // input: CPU,  output: node
		std::vector<std::vector<cpu_t>> node_cpu_map; // input: node, output: list of CPUs

		// To know where each TID is (in terms of CPUs and node)
		std::vector<set<pid_t>> cpu_tid_map;  // input: CPU,  output: list of TIDs
		std::vector<set<pid_t>> node_tid_map; // input: node, output: list of TIDs

		process_tree proc_tree; // processes tree

	} // namespace details

	namespace auxiliary_functions {
		[[nodiscard]] inline auto allowed_nodes() -> std::vector<node_t> {
			std::vector<node_t> allowed_nodes;

			auto * allowed_nodes_mask = numa_get_mems_allowed();

			for (const std::weakly_incrementable auto node : std::ranges::iota_view(0UL, allowed_nodes_mask->size)) {
				if (std::cmp_not_equal(numa_bitmask_isbitset(allowed_nodes_mask, node), 0)) {
					allowed_nodes.emplace_back(node);
				}
			}

			numa_bitmask_free(allowed_nodes_mask);

			return allowed_nodes;
		}

		[[nodiscard]] inline auto allowed_cpus() -> std::vector<cpu_t> {
			std::vector<cpu_t> allowed_cpus;

			const auto * const allowed_cpus_bm = numa_all_cpus_ptr;

			for (const std::weakly_incrementable auto cpu : std::ranges::iota_view(0UL, allowed_cpus_bm->size)) {
				if (std::cmp_not_equal(numa_bitmask_isbitset(allowed_cpus_bm, cpu), 0)) {
					allowed_cpus.emplace_back(cpu);
				}
			}

			return allowed_cpus;
		}

		[[nodiscard]] inline auto cpus_from_node(const node_t node) -> std::vector<cpu_t> {
			std::vector<cpu_t> cpus_in_node;

			bitmask * cpus_bm = numa_allocate_cpumask();

			if (std::cmp_equal(numa_node_to_cpus(node, cpus_bm), -1)) {
				std::cerr << "Error retrieving cpus from node " << node << ": " << strerror(errno) << '\n';
				numa_free_cpumask(cpus_bm);
				return {};
			}

			for (const std::weakly_incrementable auto cpu : std::ranges::iota_view(0UL, cpus_bm->size)) {
				if (std::cmp_not_equal(numa_bitmask_isbitset(cpus_bm, cpu), 0) &&
				    std::find(details::cpus.begin(), details::cpus.end(), cpu) != details::cpus.end()) {
					cpus_in_node.emplace_back(cpu);
				}
			}

			numa_free_cpumask(cpus_bm);

			return cpus_in_node;
		}

		// Retrieve information about the architecture of the system (#nodes, #cpus, cpus at every node, etc.)
		auto detect_system_NUMA() -> bool {
			details::nodes = allowed_nodes();
			details::cpus  = allowed_cpus();

			details::node_cpu_map.resize(max_node() + 1, {});
			for (const auto node : details::nodes) {
				details::node_cpu_map[node] = auxiliary_functions::cpus_from_node(node);
				if (details::node_cpu_map[node].empty()) { return false; }
			}

			// For each CPU, reads topology file to get package (node) id
			details::cpu_node_map.resize(num_of_cpus(), 0);
			for (const auto cpu : details::cpus) {
				details::cpu_node_map[cpu] = numa_node_of_cpu(cpu);
			}

			// Compute the lists of nodes sorted by distance from a given node...
			details::nodes_by_distance.resize(num_of_nodes(), {});
			for (const auto node : details::nodes) {
				std::multimap<int, node_t> distance_nodes_map{};

				for (const auto node_2 : details::nodes) {
					distance_nodes_map.insert({ numa_distance(node, node_2), node_2 });
				}

				// Vector of nodes sorted by distances
				std::vector<node_t> nodes_by_distance;
				nodes_by_distance.reserve(details::nodes.size());

				for (const auto & [distance, node_2] : distance_nodes_map) {
					nodes_by_distance.emplace_back(node_2);
				}

				details::nodes_by_distance[node] = nodes_by_distance;
			}

			details::cpu_tid_map.resize(num_of_cpus(), {});
			details::node_tid_map.resize(num_of_nodes(), {});

			return true;
		}

		auto detect_system_UMA() -> bool {
			details::nodes = { 0 };
			details::cpus  = allowed_cpus();

			details::node_cpu_map.resize(max_node() + 1, {});
			details::node_cpu_map[0] = details::cpus;

			details::cpu_node_map.resize(max_cpu() + 1, 0);
			for (const auto & cpu : details::cpus) {
				details::cpu_node_map[cpu] = details::nodes.front();
			}

			// Compute the lists of nodes sorted by distance from a given node...
			details::nodes_by_distance.resize(max_node() + 1, {});
			details::nodes_by_distance[0] = { 0 };

			details::cpu_tid_map.resize(max_cpu() + 1, {});
			details::node_tid_map.resize(max_node() + 1, {});

			return true;
		}
	} // namespace auxiliary_functions

	auto detect_system() noexcept -> bool {
		bool ret_value = false;

		if (std::cmp_less(numa_available(), 0)) {
			std::cerr << "NUMA support not available. Assuming you are using a UMA machine. Trying to survive..."
			          << '\n';

			ret_value = auxiliary_functions::detect_system_UMA();
		} else {
			ret_value = auxiliary_functions::detect_system_NUMA();
		}

		if (verbose::print_with_lvl(verbose::LVL1)) { print_system_topology(); }

		return ret_value;
	}

	// Functions to scan files in /proc to search for children threads and processes
	void scan_children_file(const std::filesystem::path & path, process * parent) {
		std::ifstream file(path / "children");

		while (file.good()) {
			pid_t child = 0;
			file >> child;

			// If End Of File (EOF) reached...
			if (file.eof()) {
				break; // Break the loop
			}

			process & child_proc = details::proc_tree.insert(child, parent);

			const auto subdir = std::string(process::DEFAULT_PROC) + "/" + std::to_string(child) + "/task";

			scan_dir(subdir, &child_proc);
		}
	}

	void scan_dir(const std::filesystem::path & dir, process * parent) {
		if (!std::filesystem::is_directory(dir)) { return; }

		for (const auto & content : std::filesystem::directory_iterator(dir)) {
			if (!content.is_directory()) { continue; }

			const auto subdir = content.path().filename().string();

			pid_t pid = 0;

			try {
				pid = std::stoi(subdir);
			} catch (...) {
				// Just ignore this. We found a non-number directory or self-directory/parent
				continue;
			}

			// If self thread task...
			if (parent != nullptr && std::cmp_equal(pid, parent->pid())) {
				// Search for children...
				scan_children_file(content.path(), parent);
				// ... and skip the rest of the work
				continue;
			}

			process * proc = &details::proc_tree.insert(pid, parent, dir.string());

			scan_children_file(content.path(), proc);

			scan_dir(content.path() / "task", proc);
		}
	}

} // namespace system_info