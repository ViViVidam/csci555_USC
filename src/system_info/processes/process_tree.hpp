/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_PROCESS_TREE_HPP
#define THANOS_PROCESS_TREE_HPP

#include <map>         // for map, operator==, _Rb_tree_iterator, _Rb_tree_...
#include <memory>      // for unique_ptr
#include <ostream>     // for operator<<, ostream, basic_ostream, char_traits
#include <ranges>      // for ranges::iota_view...
#include <set>         // for set
#include <string_view> // for string_view
#include <type_traits> // for __strip_reference_wrapper<>::__type
#include <unistd.h>    // for pid_t, getpid, size_t
#include <utility>     // for pair, make_pair, move, tuple_element<>::type
#include <vector>      // for vector

#include "process.hpp" // for process, process::DEFAULT_PROC, operator<<

class process_tree {
private:
#ifdef ASCII_TREE
	static constexpr const char * TREE_STR_HORZ = "-"; // TREE_STR_HORZ
	static constexpr const char * TREE_STR_VERT = "|"; // TREE_STR_VERT
	static constexpr const char * TREE_STR_RTEE = "`"; // TREE_STR_RTEE
	static constexpr const char * TREE_STR_BEND = "`"; // TREE_STR_BEND
	static constexpr const char * TREE_STR_TEND = ","; // TREE_STR_TEND
	static constexpr const char * TREE_STR_OPEN = "+"; // TREE_STR_OPEN
	static constexpr const char * TREE_STR_SHUT = "-"; // TREE_STR_SHUT
#else
	static constexpr const char * TREE_STR_HORZ = "\xe2\x94\x80"; // TREE_STR_HORZ ─
	static constexpr const char * TREE_STR_VERT = "\xe2\x94\x82"; // TREE_STR_VERT │
	static constexpr const char * TREE_STR_RTEE = "\xe2\x94\x9c"; // TREE_STR_RTEE ├
	static constexpr const char * TREE_STR_BEND = "\xe2\x94\x94"; // TREE_STR_BEND └
	static constexpr const char * TREE_STR_TEND = "\xe2\x94\x8c"; // TREE_STR_TEND ┌
	static constexpr const char * TREE_STR_OPEN = "+";            // TREE_STR_OPEN +
	static constexpr const char * TREE_STR_SHUT = "\xe2\x94\x80"; // TREE_STR_SHUT ─
#endif

	umap<pid_t, std::unique_ptr<process>> processes_ = {};

	pid_t root_;

public:
	explicit process_tree(const pid_t root = getpid(), const std::string_view dirname = process::DEFAULT_PROC) noexcept
	    :
	    root_(root) {
		try {
			std::unique_ptr<process> proc_ptr(new process{ root_, nullptr, dirname });
			processes_.insert({ root_, std::move(proc_ptr) });
		} catch (std::exception & e) {
			std::cerr << "Could not create process tree: " << e.what() << '\n';
		} catch (...) { std::cerr << "Could not create process tree..." << '\n'; }
	}

	[[nodiscard]] inline auto root() const -> pid_t {
		return root_;
	}

	[[nodiscard]] inline auto retrieve(const pid_t pid, process * parent = nullptr,
	                                   const std::string_view dirname = process::DEFAULT_PROC) -> process & {
		const auto & proc_it = processes_.find(pid);

		if (proc_it != processes_.end()) { return *proc_it->second; }

		return insert(pid, parent, dirname);
	}

	[[nodiscard]] inline auto retrieve(const pid_t pid) const -> process & {
		return *processes_.at(pid);
	}

	[[nodiscard]] inline auto retrieve() const -> process & {
		return retrieve(root_);
	}

	[[nodiscard]] inline auto retrieve_all() const -> std::vector<const process *> {
		std::vector<const process *> processes;

		for (const auto & [pid, process_ptr] : processes_) {
			processes.emplace_back(process_ptr.get());
		}

		return processes;
	}

	auto insert(const pid_t pid, process * parent = nullptr, const std::string_view dirname = process::DEFAULT_PROC)
	    -> process & {
		const auto & proc_it = processes_.find(pid);
		// Check if the process is already in the tree...
		if (proc_it != processes_.end()) {
			process & proc = *proc_it->second;
			return proc;
		}

		std::unique_ptr<process> proc(new process{ pid, parent, dirname });

		// If parent == nullptr and PPID != 0,
		// and PID != getpid() (no need to go up in the hierarchy)...
		if (std::cmp_not_equal(proc->pid(), getpid()) && std::cmp_not_equal(proc->ppid(), 0) && parent == nullptr) {
			// search for the parent with PID == PPID
			parent = &retrieve(proc->ppid(), nullptr, dirname);
		}
		// If a parent process was found...
		if (parent != nullptr) {
			// add this information to the corresponding structures
			proc->parent(parent);
			parent->add_children(*proc);
		}

		const auto proc_ret_it = processes_.insert({ pid, std::move(proc) });

		// proc_ret_it.first = iterator<pid, proc_ptr>
		return *(proc_ret_it.first->second);
	}

	auto update() -> bool {
		set<pid_t> procs_to_erase;

		bool success = true;

		for (auto & [pid, proc_ptr] : processes_) {
			auto & proc = *proc_ptr;
			if (!proc.update()) {
				success = false;

				procs_to_erase.insert(proc.pid());
			}
		}

		for (const auto & pid : procs_to_erase) {
			erase(pid);
		}

		return success;
	}

	inline auto is_alive(const pid_t pid) -> bool {
		const auto & proc_it = processes_.find(pid);

		if (proc_it != processes_.end()) {
			process & proc = *proc_it->second;
			return proc.valid();
		}

		return false;
	}

	void erase(const pid_t pid) {
		if (processes_.contains(pid)) {
			// Get self-process
			auto & process = *processes_[pid];

			// Remove children
			const auto children = process.children();

			for (const auto * child_ptr : children) {
				// Remove children of children...
				erase(child_ptr->pid());
			}

			// Remove self-process from parent child list
			auto * parent = process.parent();

			if (parent != nullptr) { parent->remove_children(process); }

			// Remove self-process
			processes_.erase(pid);
		}
	}

	auto erase_invalid() -> set<pid_t> {
		set<pid_t> to_delete;

		for (const auto & [pid, proc_ptr] : processes_) {
			auto & proc = *proc_ptr;

			if (!proc.valid()) {
				const auto & parent = proc.parent();

				if (parent != nullptr) { parent->remove_children(proc); }

				to_delete.insert(pid);
			}
		}

		for (const auto & pid : to_delete) {
			erase(pid);
		}

		return to_delete;
	}

	inline void print_level(std::ostream & os, const process & p, const size_t level = 0) const {
		static constexpr size_t TAB_SIZE = 3;

		for (const auto i : std::ranges::iota_view(size_t(), level * TAB_SIZE)) {
			os << ((i % TAB_SIZE == 0 && i > 0) ? TREE_STR_VERT : " ");
		}

		if (level != 0) { os << TREE_STR_RTEE << TREE_STR_HORZ << " "; }

		os << p << '\n';

		const auto children = p.children();
		for (const auto & child : children) {
			print_level(os, *child, level + 1);
		}
	}

	friend auto operator<<(std::ostream & os, const process_tree & p) -> std::ostream & {
		os << "Process tree with " << p.processes_.size() << " entries." << '\n';
		p.print_level(os, p.retrieve(p.root_));

		return os;
	}
};


#endif /* end of include guard: THANOS_PROCESS_TREE_HPP */
