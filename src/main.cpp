/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include <cerrno>    // for errno
#include <csignal>   // for sigaction, SIG...
#include <cstdlib>   // for strtol, strtod
#include <cstring>   // for strerror, strs...
#include <exception> // for exception
#include <iostream>  // for operator<<
#include <string>    // for string, operat...
#include <thread>    // for this_thread
#include <utility>   // for cmp...
#include <vector>    // for vector

#include <fcntl.h>       // for open, O_CREAT
#include <getopt.h>      // for required_argument
#include <linux/sched.h> // for SCHED_FIFO
#include <sched.h>       // for pid_t, __sched...
#include <span>          // for span
#include <sys/stat.h>    // for S_IRGRP, S_IROTH
#include <unistd.h>      // for close

#include "migration/migration.hpp"                    // for balance, add_pids
#include "migration/migration_var.hpp"                // for max_thread_mig...
#include "migration/strategies/memory_mig_strats.hpp" // for print_strategies
#include "migration/strategies/thread_mig_strats.hpp" // for print_strategies
#include "migration/tickets.hpp"                      // for read_tickets_file
#include "migration/utils/times.hpp"                  // for min_time_betwe...
#include "samples/perf_event/perf_event.hpp"          // for end, init, rea...
#include "samples/samples.hpp"                        // for PIDs_to_filter
#include "system_info/memory_info.hpp"                // for update_memory_...
#include "system_info/system_info.hpp"                // for detect_system
#include "utils/string.hpp"                           // for percentage
#include "utils/time.hpp"                             // for time_until
#include "utils/types.hpp"                            // for real_t, time_p...
#include "utils/verbose.hpp"                          // for lvl, DEFAULT_LVL

namespace {
	bool   export_chart_info_threads = false;
	bool   export_chart_info_memory  = false;
	real_t secs_between_chart_info   = 1;

	std::string THREAD_INFO_FILE_NAME = "thread_info_";
	std::string MEMORY_INFO_FILE_NAME = "memory_info_";

	std::ofstream thread_info_file;
	std::ofstream memory_info_file;

	bool use_rt_scheduling  = false;
	int  new_sched_priority = -1;

	bool shell_mode = false;

	pid_t child_process;

	time_point child_start;
	time_point child_end;
	real_t     child_secs;

	bool   redirect_stdout = false;
	bool   redirect_stderr = false;
	char * child_stdout    = nullptr;
	char * child_stderr    = nullptr;

	bool update_mem = false;

	real_t secs_update_proc = 0.1;
	real_t secs_update_mem  = migration::memory::min_time_between_migrations;

	real_t secs_before_migr = 5;

	real_t secs_between_balance = 0.5;

	real_t secs_between_samples = 0.1;

	real_t secs_between_iter = 0.1;

	char * file_read_tickets;
	char * file_write_tickets;

	// Capture the child process signal to make a clean end (closing auxiliary files, free memory, etc.)
	void clean_end(int signal, siginfo_t * siginfo, [[maybe_unused]] void * context) {
		if (siginfo != nullptr && std::cmp_equal(signal, SIGCHLD) &&
		    std::cmp_not_equal(siginfo->si_pid, child_process)) {
			// we can ignore this signal since it has been thrown by an auxiliary function of this program
			if (verbose::print_with_lvl(verbose::LVL_MAX)) {
				std::cout << "Signal \"" << strsignal(signal) << "\" (" << signal << ") received from PID "
				          << siginfo->si_pid << '\n';
			}
			return;
		}

		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << '\n'
			          << "Signal \"" << strsignal(signal) << "\" (" << signal << ") received. Ending..." << '\n';
		}

		// SIGCHLD -> a child process has ended
		if (std::cmp_equal(signal, SIGCHLD)) {
			child_end  = hres_clock::now();
			child_secs = utils::time::time_until(child_start, child_end);

			if (verbose::print_with_lvl(verbose::LVL1)) {
				std::cout << "Child process execution time: " << utils::string::to_string(child_secs, 2) << " seconds."
				          << '\n';
			}
		} else if (!samples::PIDs_to_filter.empty()) {
			for (const auto tid : samples::PIDs_to_filter) {
				kill(tid, signal);
			}
			return;
		}

		if (export_chart_info_threads) { thread_info_file.close(); }

		if (export_chart_info_memory) { memory_info_file.close(); }

		samples::end();

		migration::end();

		system_info::end();

		migration::write_tickets_file(file_read_tickets);

		if (verbose::print_with_lvl(verbose::LVL1)) { std::cout << "Exiting..." << '\n'; }

		exit(EXIT_SUCCESS);
	}

	// Redirect the output of the child process to a file
	auto redirect_output(const char * filename, const int output_fd = STDOUT_FILENO) -> bool {
		std::string stdout_file;

		if (filename != nullptr) {
			stdout_file += filename;
		} else {
			stdout_file += utils::time::now_string();

			switch (output_fd) {
				case STDOUT_FILENO:
					stdout_file += ".stdout";
					break;
				case STDERR_FILENO:
					stdout_file += ".stderr";
					break;
				default:
					std::cerr << "Unknown output file descriptor" << '\n';
					break;
			}
		}

		const auto fd = open(stdout_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		if (std::cmp_equal(fd, -1)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error creating stdout file " << stdout_file << ". Error: " << strerror(errno) << '\n';
			}
			return false;
		}

		if (std::cmp_equal(dup2(fd, output_fd), -1)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error redirecting output: " << strerror(errno) << '\n';
			}

			close(fd);

			return false;
		}

		close(fd);

		return true;
	}

	auto run_program(const std::span<char * const> args) -> bool {
		std::string command_str;
		for (auto * const arg : args) {
			if (arg != nullptr) {
				command_str += arg;
				command_str += " ";
			}
		}

		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << "Executing child process: " << command_str << '\n';
		}

		child_process = fork();

		if (std::cmp_equal(child_process, 0)) {
			if (redirect_stdout) { redirect_output(child_stdout, STDOUT_FILENO); }

			if (redirect_stderr) { redirect_output(child_stderr, STDERR_FILENO); }

			int err = {};
			if (shell_mode) {
				err = execl("/bin/sh", "sh", "-c", command_str.data(), static_cast<char *>(nullptr));
			} else {
				err = execvp(args[0], args.data());
			}

			if (std::cmp_not_equal(err, 0)) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Error executing: " << command_str << ". Error: " << strerror(errno) << '\n';
				}
				return false;
			}
		} else if (std::cmp_less(child_process, 0)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error creating child process for: " << command_str << ". Error: " << strerror(errno)
				          << '\n';
			}
			return false;
		}

		child_start = hres_clock::now();

		if (verbose::print_with_lvl(verbose::LVL1)) {
			std::cout << "Child process (PID " << child_process << ") created: " << command_str << '\n';
		}

		samples::insert_PID_to_filter(child_process);

		system_info::start_tree(child_process);

		return true;
	}

	auto change_sched_priority(const int new_policy = SCHED_FIFO, const int new_priority = new_sched_priority) -> bool {
		struct sched_param param {};

		if (std::cmp_equal(sched_getparam(0, &param), -1)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "Error getting scheduler parameters (" << strerror(errno)
				          << "). Using default policy and priority..." << '\n';
			}
			return false;
		}

		if (std::cmp_less(new_priority, 0)) {
			param.__sched_priority = (sched_get_priority_max(new_policy) + sched_get_priority_min(new_policy)) / 2;
		} else {
			param.__sched_priority = new_priority;
		}

		if (std::cmp_equal(sched_setscheduler(0, new_policy | SCHED_RESET_ON_FORK, &param), -1)) {
			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cerr << "RT scheduling could not be enabled (" << strerror(errno) << ")." << '\n';
			}
			return false;
		}

		if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
			std::cout << "RT scheduling successfully enabled with priority: " << param.__sched_priority << "." << '\n';
		}
		return true;
	}

	void setup_output_files() {
		// Prepare output files
		const auto now_str = utils::time::now_string();

		THREAD_INFO_FILE_NAME += now_str + ".csv";
		MEMORY_INFO_FILE_NAME += now_str + ".csv";

		if (export_chart_info_threads) {
			thread_info_file = std::ofstream(THREAD_INFO_FILE_NAME);
			migration::print_thread_info_header(thread_info_file);
		}

		if (export_chart_info_memory) {
			memory_info_file = std::ofstream(MEMORY_INFO_FILE_NAME);
			migration::print_memory_info_header(memory_info_file);
		}
	}

	void setup_signals() {
		// Sets up handler for some signals for a clean end
		struct sigaction act {};
		act.sa_sigaction = &clean_end;
		act.sa_flags     = SA_SIGINFO;

		sigaction(SIGCHLD, &act, nullptr);
		sigaction(SIGALRM, &act, nullptr);
		sigaction(SIGTERM, &act, nullptr);
		sigaction(SIGABRT, &act, nullptr);
		sigaction(SIGINT, &act, nullptr);
	}

	auto main_loop(const std::span<char * const> child_args) -> int {
		// Get system info
		system_info::detect_system();

		// Change priority (if required)
		if (use_rt_scheduling) { change_sched_priority(); }

		// Change update times for exporting information as accurately as possible
		if (export_chart_info_threads) { secs_update_proc = std::min(secs_between_chart_info, secs_update_proc); }
		if (export_chart_info_memory) { secs_update_mem = std::min(secs_between_chart_info, secs_update_mem); }

		// Prepare output files
		setup_output_files();
		printf("333333\n");
		// Init sampling system
		if (!samples::init()) { clean_end(SIGTERM, nullptr, nullptr); }
		printf("4444444\n");
		migration::read_tickets_file(file_read_tickets);
		printf("1111111\n");
		// Sets up handler for some signals for a clean end
		setup_signals();
		printf("222222\n");
		if (!run_program(child_args)) { clean_end(SIGTERM, nullptr, nullptr); }

		const time_point ref_time = hres_clock::now();

		time_point last_info_export  = ref_time;
		time_point last_proc_update  = ref_time;
		time_point last_mem_update   = ref_time;
		time_point last_samples_read = ref_time;
		time_point last_cpu_balance  = ref_time;

		std::this_thread::sleep_for(
		    std::chrono::microseconds(static_cast<int64_t>(secs_before_migr * utils::time::SECS_TO_USECS)));

		while (true) {
			try {
				const auto current_time = hres_clock::now();

				if (utils::time::time_until(last_proc_update, current_time) > secs_update_proc) {
					last_proc_update = current_time;

					const auto removed_pids = system_info::update(child_process);

					// Remove information for those PIDs not valid anymore
					migration::remove_invalid_pids(removed_pids);

					// If the children processes list has changed, a CPU balance is (likely) required
					if (std::cmp_greater(migration::thread::max_thread_migrations, 0) && !removed_pids.empty()) {
						migration::balance();
					}

					auto children = system_info::get_children(child_process);
					children.insert(child_process);

					migration::add_pids(children);

					samples::update_PIDs_to_filter(children);
				}

				if (update_mem && utils::time::time_until(last_mem_update, current_time) > secs_update_mem) {
					if (!memory_info::update_vmstat() && verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Could not update values from /proc/" << child_process << "/vmstat" << '\n';
					}

					last_mem_update = current_time;

					memory_info::update_memory_regions(samples::PIDs_to_filter);
					if (memory_info::fake_thp_enabled()) { memory_info::update_fake_thps(); }
				}

				if (!samples::rotate_enabled_counters()) {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Error multiplexing counters. Exiting..." << '\n';
					}
					clean_end(SIGTERM, nullptr, nullptr);
				}

				if (utils::time::time_until(last_samples_read, current_time) > secs_between_samples) {
					last_samples_read  = current_time;
					const auto samples = samples::read_samples();
					migration::process_samples(samples);
				}

				if (utils::time::time_until(last_info_export, current_time) > secs_between_chart_info) {
					last_info_export     = current_time;
					const auto timestamp = utils::time::time_until(ref_time, current_time) * 1000;

					if (export_chart_info_threads) { migration::print_thread_info(timestamp, thread_info_file); }
					if (export_chart_info_memory) { migration::print_memory_info(timestamp, memory_info_file); }
				}

				if (migration::thread::max_thread_migrations > 0 &&
				    utils::time::time_until(last_cpu_balance, current_time) > secs_between_balance) {
					last_cpu_balance = current_time;
					migration::balance();
				}

				migration::migrate(current_time);

				const auto iter_time = utils::time::time_until_now(current_time);

				if (verbose::print_with_lvl(verbose::LVL_MAX)) { system_info::print_system_status(); }

				const auto sleep_time =
				    static_cast<int64_t>((secs_between_iter - iter_time) * utils::time::SECS_TO_USECS);

				if (sleep_time > 0) { std::this_thread::sleep_for(std::chrono::microseconds(sleep_time)); }
			} catch (const std::exception & e) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cerr << "Error in main loop: " << e.what() << '\n';
				}
			} catch (...) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cerr << "Unknown error..." << '\n'; }
			}
		}
	}

	void usage(const char * const program_name) {
		std::cout << "Usage: " << program_name << " [options] <program_to_migrate>" << '\n';
		std::cout << "Options:" << '\n'
		          << '\t' << "[-h] [--help]" << '\n'
		          << '\t' << "[-b secs_between_balance] [--thread-balance secs]: real number > 0" << '\n'
		          << '\t' << "[-c] [--chart-threads]" << '\n'
		          << '\t' << "[-C] [--chart-memory]" << '\n'
		          << '\t' << "[-e[stderr_child]] [--stderr-child]" << '\n'
		          << '\t' << "[-f freq_instructions] [--freq-instr]: integer in the interval (0, 1000]" << '\n'
		          << '\t' << "[-F freq_memory] [--freq-memory]: integer in the interval (0, 1000]" << '\n'
		          << '\t' << "[-i file_tickets_read] [--tickets-read filename]" << '\n'
		          << '\t' << "[-I file_tickets_write] [--tickets-write filename]" << '\n'
		          << '\t' << "[-l minimum_latency] [--min-latency]: integer > 0" << '\n'
		          << '\t' << "[-m max_thread_migrations_per_iter] [--max-thread-migs]: integer >= 0" << '\n'
		          << '\t' << "[-M portion_memory_migrations_per_iter] [--max-memory-migs]: real within [0, 1]" << '\n'
		          << '\t' << "[-o[stdout_child]] [--stdout-child]" << '\n'
		          << '\t' << "[-P memory_prefetch_size] [--memory-prefetch]: integer >= 0" << '\n'
		          << '\t' << "[-r sampling_rate_in_secs] [--rate-sampling]: real number > 0" << '\n'
		          << '\t' << "[-R real_time_scheduling] [--real-time-sched]" << '\n'
		          << '\t' << "[-s thread_migration_strategy] [--thread-strategy]" << '\n'
		          << '\t' << "[-S memory_migration_strategy] [--memory-strategy]" << '\n'
		          << '\t' << "[-t seconds_between_thread_migs] [--thread-time]: real number > 0" << '\n'
		          << '\t' << "[-T seconds_between_memory_migs] [--memory-time]: real number > 0" << '\n'
		          << '\t' << "[--thp[=n_pages]]: opt integer >= 0. 0 = disable \"fake\" transparent huge pages." << '\n'
		          << '\t' << "[-u secs_update_proc] [--sec-update-proc]: real number > 0" << '\n'
		          << '\t' << "[-U secs_update_mem] [--sec-update-mem]: real number > 0" << '\n'
		          << '\t' << "[-v verbose_lvl] [--verbose]: integer within [" << verbose::NO_VERBOSE << ", "
		          << verbose::LVL_MAX << "]" << '\n'
		          << '\t' << "[-W wait_before_migr] [--wait-before-mig]: real >= 0" << '\n';
		std::cout << "Thread migration strategies:" << '\n';
		migration::thread::print_strategies(std::cout, "\t");
		std::cout << "Memory migration strategies:" << '\n';
		migration::memory::print_strategies(std::cout, "\t");
	}
} // END OF ANONYMOUS NAMESPACE

int main(const int argc, char * const argv[]) {
	const std::span<char * const> args(argv, argc);

	int c = 0;

	/* clang-format off */
	static const std::vector<struct option> long_options = {
		{"help",            no_argument,        nullptr, 'h' },
		{"thread-balance",  required_argument,  nullptr, 'b' },
		{"chart-threads",   no_argument,        nullptr, 'c' },
		{"chart-memory",    no_argument,        nullptr, 'C' },
		{"stderr-child",    optional_argument,  nullptr, 'e' },
		{"freq-instr",      required_argument,  nullptr, 'f' },
		{"freq-memory",     required_argument,  nullptr, 'F' },
		{"tickets-read",    required_argument,  nullptr, 'i' },
		{"tickets-write",   required_argument,  nullptr, 'I' },
		{"min-latency",     required_argument,  nullptr, 'l' },
		{"max-thread-migs", required_argument,  nullptr, 'm' },
		{"max-memory-migs", required_argument,  nullptr, 'M' },
		{"stdout-child",    optional_argument,  nullptr, 'o' },
		{"memory-prefetch", optional_argument,  nullptr, 'P' },
		{"rate-sampling",   required_argument,  nullptr, 'r' },
		{"real-time-sched", optional_argument,  nullptr, 'R' },
		{"thread-strategy", required_argument,  nullptr, 's' },
		{"memory-strategy", required_argument,  nullptr, 'S' },
		{"thread-time",     required_argument,  nullptr, 't' },
		{"memory-time",     required_argument,  nullptr, 'T' },
		{"thp",             optional_argument,  nullptr, '1' },
		{"shell",           no_argument,        nullptr, 'B' },
		{"sec-update-proc", required_argument,  nullptr, 'u' },
		{"sec-update-mem",  required_argument,  nullptr, 'U' },
		{"verbose",         required_argument,  nullptr, 'v' },
		{"wait-before-mig", required_argument,  nullptr, 'W' },
		{nullptr,           0,                  nullptr,  0  }
	};
	/* clang-format on */

	static const char * short_options = "+hb:BcCe::f:F:i:I:l:m:M:o::P:r:R::s:S:t:T:u:U:v:w:W:";

	while ((c = getopt_long(argc, argv, short_options, long_options.data(), nullptr)) != -1) {
		switch (c) {
			case 'b':
				secs_between_balance = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Minimum seconds between thread balance check: " << secs_between_balance << '\n';
				}
				break;
			case 'B':
				shell_mode = true;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Shell mode enable. Child process will be launche like: /bin/sh sh -c <child>" << '\n';
				}
				break;
			case 'c':
				export_chart_info_threads = true;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Thread info to be saved in file thread_info_[time_date].csv" << '\n';
				}
				break;
			case 'C':
				update_mem               = true;
				export_chart_info_memory = true;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Memory info to be saved in file memory_info_[time_date].csv" << '\n';
				}
				break;
			case 'e':
				redirect_stderr = true;
				if (optarg != nullptr) {
					child_stderr = optarg;
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Child process stderr to be written in: " << child_stderr << '\n';
					}
				} else {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Child process stderr to be written in default file: [time_date].stderr" << '\n';
					}
				}
				break;
			case 'f':
				samples::ins_frequency = std::stoi(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Instr. sampling frequency: " << samples::ins_frequency << '\n';
				}
				break;
			case 'F':
				samples::mem_frequency = std::stoi(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Memory sampling frequency: " << samples::mem_frequency << '\n';
				}
				break;
			case 'i':
				file_read_tickets = optarg;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "File to read tickets values: " << file_read_tickets << '\n';
				}
				break;
			case 'I':
				file_write_tickets = optarg;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "File to write tickets values: " << file_write_tickets << '\n';
				}
				break;
			case 'l':
				samples::minimum_latency = std::stoi(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Minimum latency: " << samples::minimum_latency << '\n';
				}
				break;
			case 'm':
				migration::thread::max_thread_migrations = std::stoi(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Max. number of thread migrations per iteration: "
					          << migration::thread::max_thread_migrations << '\n';
				}
				break;
			case 'M': {
				real_t percentage = std::stof(optarg);
				if (percentage < 0 || percentage > 1) {
					percentage = migration::memory::portion_memory_migrations;
					std::cerr << "Argument -M should be in the interval [0, 1]. Using default value "
					          << migration::memory::portion_memory_migrations << '\n';
				}
				migration::memory::portion_memory_migrations = percentage;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Percentage of memory pages to migrate per iteration: "
					          << utils::string::percentage(migration::memory::portion_memory_migrations) << " %"
					          << '\n';
				}
				break;
			}
			case 'o':
				redirect_stdout = true;
				if (optarg != nullptr) {
					child_stdout = optarg;
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Child process stdout to be written in: " << child_stdout << '\n';
					}
				} else {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Child process stdout to be written in default file: [time_date].stdout" << '\n';
					}
				}
				break;
			case 'P':
				migration::memory::memory_prefetch_size = std::stol(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Memory max. prefetch size: " << migration::memory::memory_prefetch_size << '\n';
				}
				break;
			case 'r':
				secs_between_samples = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Seconds between reads of samples buffer: " << secs_between_samples << '\n';
				}
				break;
			case 'R':
				use_rt_scheduling = true;
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Using RT (Real Time) scheduling" << '\n';
				}
				if (optarg != nullptr) {
					new_sched_priority = std::stoi(optarg);
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "RT priority: " << new_sched_priority << '\n';
					}
				}
				break;
			case 's': {
				const auto strategy = static_cast<migration::thread::strategy_t>(std::stol(optarg));
				migration::change_thread_strategy(strategy);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Thread migration strategy: " << migration::thread::print_strategy(strategy) << '\n';
				}
				break;
			}
			case 'S': {
				const auto strategy = static_cast<migration::memory::strategy_t>(std::stol(optarg));
				migration::change_memory_strategy(strategy);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Memory migration strategy: " << migration::memory::print_strategy(strategy) << '\n';
				}
				break;
			}
			case 't':
				migration::thread::min_time_between_migrations = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Minimum time (seconds) between thread migrations: "
					          << migration::thread::min_time_between_migrations << '\n';
				}
				break;
			case 'T':
				migration::memory::min_time_between_migrations = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Minimum time (seconds) between memory migrations: "
					          << migration::memory::min_time_between_migrations << '\n';
				}
				break;
			case '1':
				if (optarg != nullptr) {
					memory_info::details::fake_thp_size = std::stol(optarg);
					update_mem                          = true;
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Fake THPs enabled with size: " << memory_info::details::fake_thp_size << '\n';
						std::cout << "Memory info from /proc will be updated every " << secs_update_mem << " s" << '\n';
					}
				} else {
					const auto thp_size_str =
					    utils::cmd::exec("grep 'Hugepagesize:' /proc/meminfo | awk '{print($2)}'");
					const auto thp_size_kB = std::stol(thp_size_str);
					const auto pagesize    = memory_info::pagesize;

					const auto thp_size_n_pages = thp_size_kB * 1024 / pagesize;

					memory_info::details::fake_thp_size = thp_size_n_pages;
					update_mem                          = true;
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cout << "Fake THPs enabled with size: " << memory_info::details::fake_thp_size << '\n';
						std::cout << "Memory info from /proc will be updated every " << secs_update_mem << " s" << '\n';
					}
				}
				break;
			case 'u':
				secs_update_proc = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Seconds between updates of process tree: " << secs_update_proc << '\n';
				}
				break;
			case 'U':
				update_mem      = true;
				secs_update_mem = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Seconds between updates of memory info: " << secs_update_mem << '\n';
				}
				break;
			case 'v':
				verbose::change(std::stoi(optarg));
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Verbose level: " << verbose::lvl << '\n';
				}
				break;
			case 'W':
				secs_before_migr = std::stof(optarg);
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
					std::cout << "Seconds before starting migrations: " << secs_before_migr << '\n';
				}
				break;
			case 'h':
			case '?':
				usage(args[0]);
				exit(EXIT_SUCCESS);
			default:
				std::cerr << "Unknown option: " << c << '\n';
				exit(EXIT_FAILURE);
		}
	}

	if (verbose::print_with_lvl(verbose::LVL1)) {
		std::cout << "Thread migration algorithm: " << migration::thread::print_strategy(migration::thread::strategy)
		          << '\n';
		std::cout << "Memory migration algorithm: " << migration::memory::print_strategy(migration::memory::strategy)
		          << '\n';
		std::cout << "Time between thread migrations: "
		          << utils::string::to_string(migration::thread::min_time_between_migrations, 2) << "s" << '\n';
		std::cout << "Time between memory migrations: "
		          << utils::string::to_string(migration::memory::min_time_between_migrations, 2) << "s" << '\n';
		std::cout << "Max. thread migrations: " << utils::string::to_string(migration::thread::max_thread_migrations)
		          << '\n';
		std::cout
		    << "Max. memory migrations: " << utils::string::percentage(migration::memory::portion_memory_migrations, 2)
		    << "%" << '\n';
		std::cout << "Prefetch size: " << utils::string::to_string(migration::memory::memory_prefetch_size, 0) << '\n';
	}

	if (migration::memory::portion_memory_migrations > 0) {
		update_mem      = true;
		secs_update_mem = std::min(secs_update_mem, migration::memory::min_time_between_migrations);
	}

	secs_between_iter = std::min({ secs_between_samples, secs_between_balance, secs_between_chart_info,
	                               secs_update_proc, secs_update_mem, migration::thread::min_time_between_migrations,
	                               migration::memory::min_time_between_migrations });

	const std::span<char * const> child_args(argv + optind, argv + argc);

	return main_loop(child_args);
}
