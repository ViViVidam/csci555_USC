/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_PROCESS_HPP
#define THANOS_PROCESS_HPP

#include <cerrno>      // for errno, EFAULT, EINVAL, EPERM, ESRCH
#include <cmath>       // for isnormal
#include <cstring>     // for strerror, size_t
#include <ctime>       // for difftime, time
#include <exception>   // for exception
#include <features.h>  // for __glibc_unlikely
#include <fstream>     // for ifstream
#include <iomanip>     // for operator<<, setw
#include <iostream>    // for operator<<, ifstream, basic_ostream
#include <map>         // for allocator, map, operator==, _Rb_tree_co...
#include <numa.h>      // for numa_free_cpumask, numa_node_of_cpu
#include <ranges>      // for ranges::iota_view...
#include <sched.h>     // for sched_setaffinity, cpu_set_t, sched_get...
#include <string>      // for operator+, string, operator<<, to_string
#include <string_view> // for string_view, operator<<
#include <sys/stat.h>  // for stat
#include <tuple>       // for _Swallow_assign, ignore
#include <unistd.h>    // for pid_t, getuid, sysconf, size_t, uid_t
#include <vector>      // for vector

#include "utils/cmd.hpp"        // for exec
#include "utils/string.hpp"     // for percentage
#include "utils/time.hpp"       // for time_until
#include "utils/types.hpp"      // for real_t, cpu_t, node_t
#include "utils/verbose.hpp"    // for lvl, LVL1, LVL_MAX

class process {
public:
	constexpr static const std::string_view DEFAULT_PROC = "/proc";
	constexpr static const std::string_view DEF_CPU_STAT = "/proc/stat";

	constexpr static const char RUNNING_CHAR  = 'R';
	constexpr static const char SLEEPING_CHAR = 'S';
	constexpr static const char WAITING_CHAR  = 'W';
	constexpr static const char ZOMBIE_CHAR   = 'Z';
	constexpr static const char STOPPED_CHAR  = 'T';

	constexpr static const real_t MIN_UPDATE_TIME = 1;

private:
	process *              parent_   = nullptr;
	umap<pid_t, process *> children_ = {};

	/* clang-format off */
	pid_t                  pid_{};                 // The process ID.

	std::string            cmdline_{};             // Command line for the process

	std::string            stat_file_name_{};

	std::ifstream          stat_file_{};

	bool                   migratable_ = false;
	bool                   pinned_     = false;
	bool                   lwp_        = false;    // Is a Light Weight Process (or a thread). True if this process has the same command line as its parent or "ps" command is empty.

	char                   state_{};               // State of the process.

	pid_t                  ppid_{};                // The PID of the parent of this process.
	unsigned int           pgrp_{};                // The process group ID of the process.
	unsigned int           session_{};             // The session ID of the process.
	unsigned int           tty_nr_{};              // The controlling terminal of the process.
	int                    tpgid_{};               // The ID of the foreground process group of the controlling terminal of the process.

	unsigned long int      flags_{};               // The kernel flags word of the process.

	unsigned long int      minflt_{};              // The number of minor faults the process has made.
	unsigned long int      cminflt_{};             // The number of minor faults that the process's waited-for children have made.
	unsigned long int      majflt_{};              // The number of major faults the process has made.
	unsigned long int      cmajflt_{};             // The number of major faults that the process's waited-for children have made.

	unsigned long long     utime_{};               // Amount of time that this process has been scheduled in user mode.
	unsigned long long     stime_{};               // Amount of time that this process has been scheduled in kernel mode.
	unsigned long long     cutime_{};              // Amount of time that this process's waited-for children have been scheduled in user mode.
	unsigned long long     cstime_{};              // mount of time that this process's waited-for children have been scheduled in kernel mode.

	unsigned long long     time_{};                // Amount of time that this process has been scheduled in user and kernel mode.

	long int               priority_{};            // For processes running a real-time scheduling policy, this is the negated scheduling priority, minus one.
	long int               nice_{};                // The nice value, a value in the range 19 (low priority) to -20 (high priority).

	long int               num_threads_{};         // Number of threads in this process.

	unsigned long long     starttime_{};           // The time the process started after system boot.

	uid_t                  st_uid_{};              // User ID the process belongs to.
	int                    processor_{};           // CPU number last executed on.
	int                    pinned_processor_{};    // CPU number pinned on. There might be a delay between pinning a process and the migration is performed.
	int                    numa_node_{};           // NUMA node of processor_ field.
	int                    pinned_numa_node_{};    // NUMA node of pinned_processor_ field.  There might be a delay between pinning a process and the migration is performed.

	time_point             last_update_{};         // Time of the last update.
	unsigned long long     last_times_{};          // (utime + stime). Updated when the process is updated.
	real_t                 cpu_use_{};             // Portion of CPU time used (between 0 and 1).

	int                    exit_signal_{};         // The thread's exit status in the form reported by wait_pid.

	unsigned long long int last_total_time_{};     // Last total time of the CPU. Used for the calculation of cpu_use_.

	bool                   valid_ = false;         // Check if /proc/PID/stat has been correctly parsed or not
	/* clang-format on */

	[[nodiscard]] static inline auto uid() -> uid_t {
		static const auto UID = getuid();
		return UID;
	}

	static void set_affinity_error(const pid_t pid) {
		switch (errno) {
			case EFAULT:
				std::cerr << "Error setting affinity: A supplied memory address was invalid." << '\n';
				break;
			case EINVAL:
				std::cerr
				    << "Error setting affinity: The affinity bitmask mask contains no processors that are physically"
				       " on the system, or cpusetsize is smaller than the size of the affinity mask used by the kernel."
				    << '\n';
				break;
			case EPERM:
				std::cerr << "Error setting affinity: The calling process does not have appropriate privileges for PID "
				          << pid << "," << '\n';
				break;
			case ESRCH: // When this happens, it's practically unavoidable
				std::cerr << "Error setting affinity: The process whose ID is " << pid << " could not be found" << '\n';
				break;
		}
	}

	inline void obtain_cmdline() {
		const std::string command = "ps -p " + std::to_string(pid_) + " -o args h";

		try {
			cmdline_ = utils::cmd::exec(command);
		} catch (std::exception & e) {
			if (verbose::print_with_lvl(verbose::LVL1)) { std::cerr << e.what() << '\n'; }
		} catch (...) {
			if (verbose::print_with_lvl(verbose::LVL1)) {
				std::cerr << "Could not retrieve cmdline from PID " << pid_ << '\n';
			}
		}
	}

	auto read_stat_file() -> bool {
		if (!stat_file_.is_open() || !stat_file_.good()) { return false; }

		pid_t pid = 0;
		stat_file_ >> pid;
		if (std::cmp_not_equal(pid, pid_)) {
			// get back to the beginning of the file for future reads
			stat_file_.clear();
			stat_file_.seekg(0, std::ifstream::beg);

			return false;
		}

		std::string name;
		std::getline(stat_file_, name, ' '); // Skip space before name
		std::getline(stat_file_, name, ' '); // get the name in the format -> "(name)"

		stat_file_ >> state_;
		stat_file_ >> ppid_;
		stat_file_ >> pgrp_;
		stat_file_ >> session_;
		stat_file_ >> tty_nr_;
		stat_file_ >> tpgid_;
		stat_file_ >> flags_;
		stat_file_ >> minflt_;
		stat_file_ >> cminflt_;
		stat_file_ >> majflt_;
		stat_file_ >> cmajflt_;
		stat_file_ >> utime_;
		stat_file_ >> stime_;
		stat_file_ >> cutime_;
		stat_file_ >> cstime_;
		stat_file_ >> priority_;
		stat_file_ >> nice_;
		stat_file_ >> num_threads_;

		// skip (21) itrealvalue
		int it_real_value = 0;
		stat_file_ >> it_real_value;

		stat_file_ >> starttime_;

		// skip from (23) vsize to (37) cnswap
		constexpr size_t skip_fields = 37 - 23;
		for (size_t i = 0; i <= skip_fields; ++i) {
			unsigned long skip = 0;
			stat_file_ >> skip;
		}

		stat_file_ >> exit_signal_;

		time_ = utime_ + stime_;

		stat_file_ >> processor_;
		numa_node_ = numa_node_of_cpu(processor_);

		if (!pinned_) {
			pinned_processor_ = processor_;
			pinned_numa_node_ = numa_node_;
		}

		const time_point curr_time = hres_clock::now();
		if (utils::time::time_until(last_update_, curr_time) > MIN_UPDATE_TIME) {
			last_update_ = curr_time;

			const auto period = scan_cpu_time();

			cpu_use_ = static_cast<real_t>(time_ - last_times_) / period;
			if (!std::isnormal(cpu_use_)) { cpu_use_ = 0.0; }

			// Parent processes gather all children CPU usage => cpu_use_ >> 1
			static constexpr real_t MARGIN_OF_ERROR = 1.1; // 1.1 for giving a margin of error
			if (cpu_use_ > MARGIN_OF_ERROR) { cpu_use_ = cpu_use_ / static_cast<real_t>(num_threads_); }

			last_times_ = time_;
		}

		// get back to the beginning of the file for future reads
		stat_file_.clear();
		stat_file_.seekg(0, std::ifstream::beg);

		return true;
	}

	auto scan_cpu_time(const std::string_view filename = DEF_CPU_STAT) -> real_t {
		static const auto N_CPUS = sysconf(_SC_NPROCESSORS_ONLN);

		std::ifstream file(filename.data(), std::ios::in);

		if (!file.is_open()) {
			std::cerr << "Cannot open file: " << filename << '\n';
			return {};
		}

		if (__glibc_unlikely(std::cmp_less_equal(N_CPUS, 0))) {
			std::cerr << "Invalid number of CPUs: " << N_CPUS << '\n';
			return {};
		}

		std::string cpu_str; // = "cpu "

		unsigned long long int user_time   = 0;
		unsigned long long int nice_time   = 0;
		unsigned long long int system_time = 0;
		unsigned long long int idle_time   = 0;
		unsigned long long int io_wait     = 0;
		unsigned long long int irq         = 0;
		unsigned long long int soft_irq    = 0;
		unsigned long long int steal       = 0;
		unsigned long long int guest       = 0;
		unsigned long long int guest_nice  = 0;

		file >> cpu_str;
		file >> user_time;
		file >> nice_time;
		file >> system_time;
		file >> idle_time;
		file >> io_wait;
		file >> irq;
		file >> soft_irq;
		file >> steal;
		file >> guest;
		file >> guest_nice;

		// Guest time is already accounted in user time
		user_time = user_time - guest;
		nice_time = nice_time - guest_nice;

		const auto idle_all_time   = idle_time + io_wait;
		const auto system_all_time = system_time + irq + soft_irq;
		const auto virt_all_time   = guest + guest_nice;
		const auto total_time      = user_time + nice_time + system_all_time + idle_all_time + steal + virt_all_time;

		const auto total_period = (total_time > last_total_time_) ? (total_time - last_total_time_) : 1;

		last_total_time_ = total_time;

		const auto period = static_cast<real_t>(total_period) / static_cast<real_t>(N_CPUS);

		return period;
	}

	inline auto is_migratable() -> bool {
		// Bad PID
		if (__glibc_unlikely(std::cmp_less(pid_, 1))) { return false; }
		// Root can do anything (this tool is supposed to be executed as normal user, not root)
		if (std::cmp_equal(uid(), 0)) { return true; }

		const std::string folder = "/proc/" + std::to_string(pid_);

		struct stat info = {};

		stat(folder.c_str(), &info);

		st_uid_ = info.st_uid;

		return st_uid_ == uid();
	}

public:
	process()             = delete;
	process(process & p)  = delete;
	process(process && p) = delete;

	auto operator=(const process & p) -> process & = delete;
	auto operator=(process &&) -> process &        = default;

	explicit process(const pid_t pid, process * parent = nullptr,
	                 const std::string_view dirname = DEFAULT_PROC) noexcept :
	    parent_(parent),
	    pid_(pid),
	    stat_file_name_(std::string(dirname) + "/" + std::to_string(pid) + "/stat"),
	    stat_file_(stat_file_name_),
	    valid_(read_stat_file()) {
		if (valid_) {
			obtain_cmdline();

			migratable_ = is_migratable();

			if (parent != nullptr) {
				parent->add_children(*this);

				if (cmdline_.empty() || cmdline_ == parent->cmdline_) {
					lwp_     = true;
					cmdline_ = parent_->cmdline_;
				}

				if (verbose::print_with_lvl(verbose::LVL_MAX) && lwp_) {
					if (parent != nullptr) {
						std::cout << "PID " << pid << " has been considered a LWP. Children of " << parent->pid()
						          << '\n';
					}
					std::cout << "PID " << pid << " has been considered a LWP with no parent..." << '\n';
				}
			}
		}
	}

	~process() {
		std::ignore = unpin(false);
	};

	inline void parent(process * p) {
		parent_ = p;
	}

	[[nodiscard]] inline auto parent() -> process * {
		return parent_;
	}

	[[nodiscard]] inline auto parent() const -> const process * {
		return parent_;
	}

	[[nodiscard]] inline auto cmdline() const -> const std::string & {
		return cmdline_;
	}

	[[nodiscard]] inline auto pid() const -> pid_t {
		return pid_;
	}

	[[nodiscard]] inline auto ppid() const -> pid_t {
		return ppid_;
	}

	[[nodiscard]] inline auto lwp() const -> bool {
		return lwp_;
	}

	[[nodiscard]] inline auto cpu() const -> cpu_t {
		return processor_;
	}

	[[nodiscard]] inline auto pinned_cpu() const -> cpu_t {
		return pinned_processor_;
	}

	[[nodiscard]] inline auto node() const -> node_t {
		return numa_node_;
	}

	[[nodiscard]] inline auto pinned_node() const -> node_t {
		return pinned_numa_node_;
	}

	// Usage of CPU in [0, 1]. 0 = no computation, 1 = 100% of CPU time.
	// Values >1 can be obtained for multi-threaded processes.
	[[nodiscard]] inline auto cpu_use() const -> real_t {
		return cpu_use_;
	}

	[[nodiscard]] inline auto state() const -> char {
		return state_;
	}

	[[nodiscard]] inline auto valid() const -> bool {
		return valid_;
	}

	[[nodiscard]] inline auto priority() const {
		return priority_;
	}

	[[nodiscard]] inline auto is_pinned() const -> bool {
		return pinned_;
	}

	// Get direct children
	[[nodiscard]] inline auto children() const -> std::vector<const process *> {
		std::vector<const process *> ret_children;
		for (const auto & [pid, child] : children_) {
			ret_children.emplace_back(child);
		}
		return ret_children;
	}

	// Get direct children
	[[nodiscard]] inline auto children() -> std::vector<process *> {
		std::vector<process *> ret_children;
		for (const auto & [pid, child] : children_) {
			ret_children.emplace_back(child);
		}
		return ret_children;
	}

	// Get children, grandchildren, great-grandchildren...
	[[nodiscard]] inline auto all_children() const -> std::vector<const process *> {
		std::vector<const process *> ret_children;
		for (const auto & [pid, child_ptr] : children_) {
			ret_children.emplace_back(child_ptr);
			const auto & child = *child_ptr;
			const auto   aux   = child.all_children();
			ret_children.insert(ret_children.end(), aux.begin(), aux.end());
		}
		return ret_children;
	}

	// Get children, grandchildren, great-grandchildren...
	[[nodiscard]] inline auto all_children() -> std::vector<process *> {
		std::vector<process *> ret_children;
		for (const auto & [pid, child] : children_) {
			ret_children.emplace_back(child);
			const auto aux = child->all_children();
			ret_children.insert(ret_children.end(), aux.begin(), aux.end());
		}
		return ret_children;
	}

	inline void add_children(process & child) {
		children_[child.pid_] = &child;
	}

	inline void remove_children(process & child) {
		children_.erase(child.pid_);
	}

	[[nodiscard]] inline auto is_running() const -> bool {
		return state_ == RUNNING_CHAR;
	}

	[[nodiscard]] inline auto migratable() const -> bool {
		return migratable_;
	}

	inline auto update() -> bool {
		return valid_ = read_stat_file();
	}

	inline auto update_all() -> bool {
		bool success = valid_ = read_stat_file();

		for (const auto & [tid, child] : children_) {
			success &= child->update();
		}

		return success;
	}

	inline auto pin(const cpu_t cpu, const bool print = true) -> bool {
		if (std::cmp_equal(cpu, processor_) && pinned_) { return true; }

		cpu_set_t affinity;

		CPU_ZERO(&affinity);
		CPU_SET(cpu, &affinity);

		if (__glibc_unlikely(sched_setaffinity(pid_, sizeof(cpu_set_t), &affinity))) {
			if (print) { set_affinity_error(pid_); }
			return false;
		}

		pinned_           = true;
		pinned_processor_ = cpu;
		pinned_numa_node_ = numa_node_of_cpu(processor_);

		return true;
	}

	inline auto pin(const bool print = true) -> bool {
		return pin(pinned_processor_, print);
	}

	inline auto pin_node(const node_t node, const bool print = true) -> bool {
		bitmask * cpus = numa_allocate_cpumask();

		if (__glibc_unlikely(std::cmp_equal(numa_node_to_cpus(node, cpus), -1))) {
			if (print) { std::cerr << "Error: " << strerror(errno) << '\n'; }
			numa_free_cpumask(cpus);
			return false;
		}

		if (__glibc_unlikely(numa_sched_setaffinity(pid_, cpus))) {
			if (print) { set_affinity_error(pid_); }
			numa_free_cpumask(cpus);
			return false;
		}

		pinned_           = true;
		pinned_numa_node_ = node;

		for (const auto cpu : std::ranges::iota_view(0UL, cpus->size)) {
			if (std::cmp_not_equal(numa_bitmask_isbitset(cpus, cpu), 0)) {
				pinned_processor_ = static_cast<cpu_t>(cpu);
				break;
			}
		}

		numa_free_cpumask(cpus);

		return true;
	}

	inline auto pin_node(const bool print = true) -> bool {
		return pin_node(pinned_numa_node_, print);
	}

	[[nodiscard]] inline auto unpin(const bool print = true) const -> bool {
		if (!pinned_) { return true; }

		cpu_set_t affinity;
		sched_getaffinity(0, sizeof(cpu_set_t), &affinity); // Gets profiler's affinity (supposed to be the default)

		if (__glibc_unlikely(sched_setaffinity(pid_, sizeof(cpu_set_t), &affinity))) {
			if (print) { set_affinity_error(pid_); }
			return false;
		}

		return true;
	}

	friend auto operator<<(std::ostream & os, const process & p) -> std::ostream & {
		os.precision(1);
		os << std::fixed;

		/* clang-format off */
		os << "PID: "   << std::setw(5) << p.pid_ <<
        	", PPID: "  << std::setw(5) << p.ppid_ <<
        	", NODE: "  << std::setw(1) << p.numa_node_ <<
        	", CPU: "   << std::setw(3) << p.processor_ <<
        	" (";
		/* clang-format on */

		if (p.cpu_use_ >= 1.0) {
			// E.g. : (113.%)
			os << std::setw(3) << utils::string::percentage(p.cpu_use_, 0) << ".";
		} else {
			// E.g. : (73.8%)
			os << std::setw(4) << utils::string::percentage(p.cpu_use_, 1);
		}

		/* clang-format off */
		os << "%)" << ", STATE: " << p.state_ << ", LWP: " << p.lwp_ << ", CMDLINE: " << p.cmdline_;
		/* clang-format on */

		os << std::defaultfloat;

		return os;
	}
};

#endif /* end of include guard: THANOS_PROCESS_HPP */
