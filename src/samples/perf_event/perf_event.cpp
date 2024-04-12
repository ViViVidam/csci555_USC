/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#include "perf_event.hpp"

#include <fcntl.h>
#include <features.h>           // for __glibc_unlikely
#include <linux/perf_event.h>   // for perf_event_attr, perf_event_attr...
#include <perfmon/perf_event.h> // for perf_event_open
#include <perfmon/pfmlib.h>     // for pfm_strerror, pfm_pmu_info_t
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h> // for close, read, sysconf, _SC_PAGESIZE

#include <cerrno>   // for errno, EINTR, EOPNOTSUPP
#include <cstdint>  // for uint64_t
#include <cstdio>   // for stderr
#include <cstdlib>  // for exit, EXIT_FAILURE
#include <cstring>  // for strerror, memset, strlen
#include <iostream> // for operator<<, basic_ostream, basic...
#include <span>     // for span
#include <utility>  // for move, cmp

#include "perf_event/perf_util.hpp" // for perf_event_desc_t, (anonymous)
#include "samples.hpp"              // for pebs, NUM_GROUPS, buffer_reads
#include "system_info.hpp"          // for num_of_cpus
#include "verbose.hpp"              // for lvl, DEFAULT_LVL, LVL_MAX, LVL1

namespace samples {
	namespace {
		constexpr size_t MAX_FAILURES_BEFORE_REBOOT = 100;

		size_t NUM_FAILURES = 0;

		template<std::size_t sz, typename T = int>
		constexpr auto range() -> std::array<T, sz> {
			std::array<T, sz> array{};
			std::iota(array.begin(), array.end(), 0);
			return array;
		}

		constexpr auto groups = range<NUM_GROUPS>();

		std::vector<pollfd> poll_fds; // Poll File Descriptors

		std::array<std::vector<std::span<perf_event_desc_t>>, NUM_GROUPS>
		    all_fds; // Perf events File Descriptors -> all_fds[group][cpu][event_in_group]

		int num_buffers; // Total number of buffers

		bool ENABLE_KERNEL_MODE;

		int                          MAX_HW_COUNTERS;
		int                          NUM_AVAILABLE_COUNTERS;
		std::array<bool, NUM_GROUPS> AVAILABLE_COUNTERS;

		int  LAST_HWC_ENABLED;
		bool ENABLE_MULTIPLEXING;

		std::array<std::vector<uint64_t>, NUM_GROUPS> samples_last_values;
		std::array<std::vector<uint64_t>, NUM_GROUPS> samples_last_times;

		std::array<uint64_t, NUM_GROUPS> collected_samples_group;
		std::array<uint64_t, NUM_GROUPS> processed_samples_group;
		std::array<uint64_t, NUM_GROUPS> lost_samples_group;

		uint64_t unknown_samples;
		uint64_t discarded_samples;

		std::array<uint64_t, NUM_GROUPS> buffer_reads;

		const auto page_size = sysconf(_SC_PAGESIZE);
		// +1 for header information
		const auto map_size  = (MMAP_PAGES + 1) * page_size;
	} // namespace

	int                         minimum_latency = 1;            // Minimum latency of memory samples (in ms)
	int                         mem_frequency   = DEFAULT_FREQ; // Frequency to be used for memory samples.
	int                         ins_frequency   = DEFAULT_FREQ; // Frequency to be used for instructions samples.
	std::array<int, NUM_GROUPS> freqs;                          // Periods of sampling (1000 Hz by default)

	namespace {
		void setup_group(std::span<perf_event_desc_t> & fds, const auto num_fds_group, const cpu_t cpu) {
			size_t i = 0;
			for (auto & fd : fds) {
				const auto is_group_leader = perf_is_group_leader(fds.data(), i);

				const int group_fd = is_group_leader ? -1 : fds[fd.group_leader].fd;

				fd.hw.disabled       = true;
				fd.hw.enable_on_exec = false;

				// set notification threshold to be halfway through the buffer
				fd.hw.wakeup_watermark = (MMAP_PAGES * page_size) / 2;
				fd.hw.watermark        = 1;

				fd.hw.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_READ | PERF_SAMPLE_TIME |
				                    PERF_SAMPLE_PERIOD | PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_ADDR | PERF_SAMPLE_CPU |
				                    PERF_SAMPLE_WEIGHT | PERF_SAMPLE_DATA_SRC;

				fd.hw.read_format = PERF_FORMAT_SCALE;

				if (std::cmp_greater(num_fds_group, 1)) {
					fd.hw.sample_type |= PERF_SAMPLE_READ;
					fd.hw.read_format |= PERF_FORMAT_GROUP | PERF_FORMAT_ID;
				}

				fd.hw.exclude_guest  = 1;
				fd.hw.exclude_kernel = (ENABLE_KERNEL_MODE ? 0 : 1);

				// Profile every PID for a given CPU
				fd.fd = perf_event_open(&fd.hw, -1, cpu, group_fd, 0);

				if (std::cmp_equal(fd.fd, -1)) {
					std::cerr << "Cannot attach event " << fd.name << " in CPU " << cpu << ". ";
					if (std::cmp_equal(errno, EOPNOTSUPP)) {
						std::cerr << "Event is not supported. ";
					} else if (fd.hw.precise_ip) {
						std::cerr << "Precise mode may not be supported. ";
					}
					std::cerr << "Error " << errno << " (" << pfm_strerror(errno) << ")" << '\n';
					exit(EXIT_FAILURE);
				} else {
					if (verbose::print_with_lvl(verbose::LVL4)) {
						std::cout << "Event " << fd.name << " successfully opened for CPU " << cpu << "." << '\n';
					}
				}

				++i;
			}
		}

		auto setup_cpu_group(const auto cpu, const auto group) -> int {
			perf_event_desc_t * fds_ptr = nullptr;

			int num_fds_group = 0;

			// Allocate fds
			auto ret = perf_setup_list_events(details::events.at(group), &fds_ptr, &num_fds_group);
			if (std::cmp_not_equal(ret, PFM_SUCCESS)) {
				std::cerr << "Cannot setup event list: " << pfm_strerror(ret) << '\n';
				exit(EXIT_FAILURE);
			} else if (std::cmp_equal(num_fds_group, 0)) {
				std::cerr << "Cannot setup event list." << '\n';
				exit(EXIT_FAILURE);
			}

			std::span<perf_event_desc_t> fds(fds_ptr, num_fds_group);

			all_fds.at(group).at(cpu) = fds;

			// Here we define special configuration for each group
			if (group == MEM_SAMPLE) { // Memory
				// Config for latency monitoring
				fds[0].hw.config1    = samples::minimum_latency;
				fds[0].hw.precise_ip = 2;
			}

			fds[0].hw.freq        = 1; // If 1, use frequency instead of period
			fds[0].hw.sample_freq = samples::freqs.at(group);

			if (std::cmp_equal(fds[0].hw.sample_freq, 0)) {
				std::cerr << "Need to set sampling period or freq on first event" << '\n';
				exit(EXIT_FAILURE);
			}

			// setup HW counters in group
			setup_group(fds, num_fds_group, cpu);

			// kernel adds the header page to the size of the memory mapped region
			fds[0].buf = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[0].fd, 0);

			if (fds[0].buf == MAP_FAILED) {
				std::cerr << "Cannot mmap buffer: " << strerror(errno) << '\n';
				exit(EXIT_FAILURE);
			}

			// does not include header page
			fds[0].pgmsk = (MMAP_PAGES * page_size) - 1;

			// send samples for all events to first event's buffer
			for (const auto & fd : fds) {
				if (std::cmp_not_equal(fd.hw.sample_freq, 0)) { continue; }

				ret = ioctl(fd.fd, PERF_EVENT_IOC_SET_OUTPUT, fds[0].fd);

				if (std::cmp_not_equal(ret, 0)) {
					std::cerr << "Cannot redirect sampling output: " << strerror(errno) << '\n';
					exit(EXIT_FAILURE);
				}
			}

			if (std::cmp_greater(num_fds_group, 1)) {
				// We are using PERF_FORMAT_GROUP, therefore the structure
				// of val is as follows:
				//   { u64           nr;
				//     { u64         time_enabled; } && PERF_FORMAT_ENABLED
				//     { u64         time_running; } && PERF_FORMAT_RUNNING
				//     { u64         value;
				//       { u64       id;           } && PERF_FORMAT_ID
				//     }             cntr[nr];
				//   }
				// We are skipping the first 3 values (nr, time_enabled, time_running)
				// and then for each event we get a pair of values.

				static constexpr auto SKIP_VALUES      = 3;
				static constexpr auto VALUES_PER_GROUP = 2;
				static constexpr auto VALUE_ID_OFFSET  = 1;

				size_t read_size = (SKIP_VALUES + VALUES_PER_GROUP * num_fds_group) * sizeof(uint64_t);

				std::vector<uint64_t> val(SKIP_VALUES + VALUES_PER_GROUP * num_fds_group);

				const auto size = read(fds[0].fd, val.data(), read_size);
				if (std::cmp_equal(size, -1)) {
					std::cerr << "Cannot read ID " << val.size() << '\n';
					exit(EXIT_FAILURE);
				}

				for (size_t i = 0; auto & fd : fds) {
					fd.id = val[SKIP_VALUES + VALUES_PER_GROUP * i + VALUE_ID_OFFSET];
					++i;
				}
			}

			return 0;
		}

		auto init_internal_variables() -> bool {
			if (std::cmp_equal(system_info::num_of_cpus(), 0)) {
				if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) { std::cerr << "System not detected" << '\n'; }
				return false;
			}

			// Initialize variables
			const auto last_v_group_cpu = [&]() {
				std::array<std::vector<uint64_t>, samples::NUM_GROUPS> v;
				v.fill(std::vector<uint64_t>(system_info::num_of_cpus(), {}));
				return v;
			};

			samples_last_values = last_v_group_cpu();
			samples_last_times  = last_v_group_cpu();

			std::fill(freqs.begin(), freqs.end(), ins_frequency);
			freqs[MEM_SAMPLE] = mem_frequency;

			std::fill(collected_samples_group.begin(), collected_samples_group.end(), 0);
			std::fill(processed_samples_group.begin(), processed_samples_group.end(), 0);
			std::fill(buffer_reads.begin(), buffer_reads.end(), 0);
			std::fill(lost_samples_group.begin(), lost_samples_group.end(), 0);

			unknown_samples = 0;

			num_buffers = static_cast<int>(system_info::num_of_cpus()) * NUM_GROUPS;

			for (auto & all_fd : all_fds) {
				all_fd = std::vector<std::span<perf_event_desc_t>>(system_info::num_of_cpus());
			}

			return true;
		}

		auto find_events() -> bool {
			for (const auto & group : groups) {
				const auto & event     = details::events.at(group);
				const auto   event_int = pfm_find_event(event);

				if (std::cmp_less(event_int, 0)) {
					std::cerr << "Event " << event << " not found. " << pfm_strerror(event_int) << '\n';
					AVAILABLE_COUNTERS.at(group) = false;
					return false;
				}

				if (verbose::print_with_lvl(verbose::LVL4)) {
					std::cout << "Event " << event << " found. " << event_int << '\n';
				}
				AVAILABLE_COUNTERS.at(group) = true;
				++NUM_AVAILABLE_COUNTERS;
			}

			return true;
		}

		auto get_events_info(const pfm_pmu_info_t & pmu_info) -> std::pair<size_t, size_t> {
			size_t supported_events = 0;
			size_t available_events = 0;

			for (auto event = pmu_info.first_event; event != -1; event = pfm_get_event_next(event)) {
				pfm_event_info_t ev_info{};
				memset(&ev_info, 0, sizeof(pfm_event_info_t));

				const auto pfm_err = pfm_get_event_info(event, PFM_OS_NONE, &ev_info);

				if (std::cmp_not_equal(pfm_err, PFM_SUCCESS)) {
					if (verbose::print_with_lvl(verbose::LVL_MAX)) {
						std::cerr << "Error in pfm_get_event_info(): " << pfm_err;
						switch (pfm_err) {
							case PFM_ERR_NOINIT:
								std::cerr << " (PFM_ERR_NOINIT)";
								break;
							case PFM_ERR_INVAL:
								std::cerr << " (PFM_ERR_INVAL)";
								break;
							case PFM_ERR_NOTSUPP:
								std::cerr << " (PFM_ERR_NOTSUPP)";
								break;
							default:
								std::cerr << " (" << pfm_strerror(pfm_err) << ")";
								break;
						}
						std::cerr << '\n';
					}
					continue;
				}

				if (verbose::print_with_lvl(verbose::LVL_MAX)) {
					std::cout << "\t\t";
					if (std::cmp_not_equal(pmu_info.is_present, 0)) {
						std::cout << "Available";
					} else {
						std::cout << "Supported";
					}
					std::cout << " event: " << pmu_info.name << "::" << ev_info.name << '\n';
					if (std::cmp_greater(strlen(ev_info.desc), 0)) {
						std::cout << "\t\t\t[" << ev_info.desc << "]" << '\n';
					}
				}

				if (std::cmp_not_equal(pmu_info.is_present, 0)) { ++available_events; }
				++supported_events;
			}

			return { available_events, supported_events };
		}

		auto get_pmu_info() {
			size_t total_supported_events = 0;
			size_t total_available_events = 0;

			if (verbose::print_with_lvl(verbose::LVL4)) { std::cout << "Detected PMU models:" << '\n'; }

			int pmu_i = 0;
			pfm_for_all_pmus(pmu_i) {
				pfm_pmu_info_t pmu_info{};

				const auto pmu = static_cast<pfm_pmu_t>(pmu_i);
				const auto ret = pfm_get_pmu_info(pmu, &pmu_info);

				if (std::cmp_not_equal(ret, PFM_SUCCESS)) { continue; }

				if (pmu_info.is_present) {
					if (verbose::print_with_lvl(verbose::LVL4)) {
						std::cout << '\t' << pmu_i << '\t' << pmu_info.name << ": " << pmu_info.desc << '\t'
						          << "Max. counters: " << pmu_info.num_cntrs << '\n';
					}

					const auto [available, supported] = get_events_info(pmu_info);

					total_available_events += available;
					total_supported_events += supported;

					MAX_HW_COUNTERS = std::max(MAX_HW_COUNTERS, pmu_info.num_cntrs);

					if constexpr (NUM_GROUPS > MAX_BROADWELL_CTRS) { // Computed at compile time
						if (pmu == PFM_PMU_INTEL_BDW || pmu == PFM_PMU_INTEL_BDW_EP) {
							ENABLE_KERNEL_MODE = true;
							if (verbose::print_with_lvl(verbose::LVL4)) {
								std::cout << "Intel Broadwell or Broadwell EP architecture detected. In order to use "
								          << NUM_GROUPS << " counters, kernel mode will be enabled." << '\n';
							}
						}
					}
				}
			}

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "Total events: " << total_available_events << " available, " << total_supported_events
				          << " supported." << '\n';
			}
		}

		auto init_pfm() -> bool {
			// Initializes PFM
			const auto err = pfm_initialize();
			if (std::cmp_not_equal(err, PFM_SUCCESS)) {
				std::cerr << "libpfm initialization failed: " << pfm_strerror(err) << '\n';
				return false;
			}

			if (!find_events()) { return false; }

			get_pmu_info();

			if (std::cmp_greater(NUM_GROUPS, MAX_HW_COUNTERS)) { ENABLE_MULTIPLEXING = true; }

			if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
				std::cout << "Available hardware counters: " << MAX_HW_COUNTERS << '\n';
				std::cout << "Events to measure: " << NUM_GROUPS << '\n';
				if (ENABLE_MULTIPLEXING) { std::cout << "Multiplexing enabled" << '\n'; }
			}

			return true;
		}

		void emergency_reboot() {
			if (verbose::print_with_lvl(verbose::LVL1)) {
				std::cerr << "Emergency reboot of sampling required: NUM_FAILURES > MAX_NUM_FAILURES" << '\n';
			}

			NUM_FAILURES = 0;

			end();
			init();
		}
	} // namespace

	auto disable_counters(const int begin, const int end) -> bool {
		for (const auto group : std::ranges::iota_view(begin, end)) {
			if (!AVAILABLE_COUNTERS.at(group)) { continue; }

			for (const auto & cpu : system_info::cpus()) {
				// Disabling only the leader is enough
				auto & fds = all_fds.at(group).at(cpu)[0];

				if (std::cmp_equal(fds.fd, -1)) { continue; }

				if (fds.hw.disabled) { continue; }

				const auto ret = ioctl(fds.fd, PERF_EVENT_IOC_DISABLE, O_NONBLOCK);

				fds.hw.disabled = true;

				if (std::cmp_not_equal(ret, 0)) {
					if (verbose::print_with_lvl(verbose::DEFAULT_LVL)) {
						std::cerr << "Cannot stop counter " << fds.name << '\n';
					}
					return false;
				}
				if (verbose::print_with_lvl(verbose::LVL_MAX)) {
					std::cout << "Disabled counter (CPU " << cpu << "): " << fds.name << '\n';
				}
			}
		}

		return true;
	}

	auto enable_counters(const int begin, const int to_enable) -> bool {
		for (int i = begin, enabled = 0; enabled < to_enable; ++i) {
			const auto group = i % NUM_GROUPS;

			if (!AVAILABLE_COUNTERS.at(group)) { continue; }

			for (const auto & cpu : system_info::cpus()) {
				// Enabling only the leader is enough
				auto & fds = all_fds.at(group).at(cpu)[0];

				if (std::cmp_equal(fds.fd, -1)) { continue; }

				if (!fds.hw.disabled) { continue; }

				const auto ret = ioctl(fds.fd, PERF_EVENT_IOC_ENABLE, O_NONBLOCK);

				fds.hw.disabled = false;

				if (std::cmp_not_equal(ret, 0)) {
					std::cerr << "Cannot start counter: " << fds.name << '\n';
					return false;
				}
				if (verbose::print_with_lvl(verbose::LVL_MAX)) {
					std::cout << "Enabled counter (CPU " << cpu << "): " << fds.name << '\n';
				}
			}

			LAST_HWC_ENABLED = group;

			++enabled;
		}

		return true;
	}

	auto rotate_enabled_counters() -> bool {
		if (!ENABLE_MULTIPLEXING) { return true; }

		const auto first_to_enable = (LAST_HWC_ENABLED + 1) % NUM_GROUPS;

		// Disable all groups
		if (!disable_counters()) { return false; }

		// Enable next set of groups
		return enable_counters(first_to_enable, std::min(MAX_HW_COUNTERS, NUM_AVAILABLE_COUNTERS));
	}

	auto init() -> bool {
		if (!init_internal_variables()) {
			std::cerr << "Could not setup sampling method..." << '\n';
			return false;
		}
		if (!init_pfm()) { return false; }
		// Sets up counter configuration
		for (const auto & cpu : system_info::cpus()) {
			for (const auto & group : groups) {
				setup_cpu_group(cpu, group);
			}
		}

		poll_fds = std::vector<pollfd>(samples::num_buffers);

		// This is for polling the buffers for the available groups
		for (size_t buffer = 0; auto & poll_fd : poll_fds) {
			const auto group = buffer / system_info::num_of_cpus();
			const auto cpu   = buffer % system_info::num_of_cpus();

			poll_fd.fd     = all_fds.at(group).at(cpu)[0].fd;
			poll_fd.events = POLLIN;

			++buffer;
		}
		// Enable counters
		if (ENABLE_MULTIPLEXING) {
			if (!rotate_enabled_counters()) {
				std::cerr << "Cannot start counters" << '\n';
				exit(EXIT_FAILURE);
			}
		} else {
			if (!enable_counters(0, std::min(NUM_AVAILABLE_COUNTERS, NUM_GROUPS))) {
				std::cerr << "Cannot start counters" << '\n';
				exit(EXIT_FAILURE);
			}
		}
		LAST_HWC_ENABLED = MAX_HW_COUNTERS - 1;

		return true;
	}

	void process_sample_buf(const cpu_t cpu, const std::span<perf_event_desc_t> & perf_event_desc,
	                        const sample_type_t type, std::vector<samples::pebs> & samples_list) {
		/* IMPORTANT!!!!
		 * First sample of each type (but memory samples) is discarded since you cannot compute a **trustable** increment
		 * value, that is, for i-th sample, its real value corresponds to sample[i].value - sample[i-1].value.
		 * Since for i = 0, you cannot have i-1, you have to discard it.
		 */

		auto & last_value = samples_last_values[type][cpu];
		auto & last_time  = samples_last_times[type][cpu];

		const auto num_fds_p = static_cast<int>(perf_event_desc.size());

		const auto sample_type = static_cast<sample_type_t>(type);

		auto * const pds_ptr = perf_event_desc.data();

		const auto idx = static_cast<int>(perf_event_desc.data() - pds_ptr);

		struct perf_event_header ehdr {};

		size_t discarded = 0;

		while (std::cmp_equal(perf_read_buffer(perf_event_desc.data(), &ehdr, sizeof(ehdr)), 0)) {
			switch (ehdr.type) {
				case PERF_RECORD_SAMPLE: {
					samples::pebs sample(sample_type);

					const auto ret = transfer_data_from_buffer_to_structure(pds_ptr, num_fds_p, idx, &ehdr, &sample);

					++collected_samples_group.at(type);

					if (__glibc_unlikely(std::cmp_not_equal(ret, 0))) {
						++NUM_FAILURES;

						// Skips entire map page
						size_t to_skip = (ehdr.size == 0) ? map_size : ehdr.size - sizeof(ehdr);

						perf_skip_buffer(perf_event_desc.data(), to_skip);

						last_value = {};
						last_time  = {};

						break;
					}

					if constexpr (filter_by_PIDs) {
						if (!accept_PID_filter(sample.tid)) { // Filter by PID
							++discarded;
							++discarded_samples;
							break;
						}
					}

					if (std::cmp_not_equal(last_time, 0)) {
						// [[likely]] since "out-of-order" samples are really rare...
						if (std::cmp_greater(sample.value, last_value)) [[likely]] {
							// Save the new value to assign it later to "last_value"
							const auto new_value = sample.value;
							const auto new_time  = sample.time_running;

							// For i-th sample, its real value corresponds to sample[i].value - sample[i-1].value
							sample.value -= last_value;
							sample.time_running -= last_time;

							// Save last value for later use
							last_value = new_value;
							last_time  = new_time;

							samples_list.emplace_back(sample);
						} else {
							last_value = 0;
							last_time  = 0;
						}
					} else {
						last_value = sample.value;
						last_time  = sample.time_running;
					}

					++processed_samples_group.at(type);
				} break;
				case PERF_RECORD_EXIT:
					//display_exit(hw, options.output_file);
					break;
				case PERF_RECORD_LOST:
					lost_samples_group.at(type) += display_lost(perf_event_desc.data(), pds_ptr, num_fds_p, stderr);
					break;
				case PERF_RECORD_THROTTLE:
					//display_freq(1, hw, options.output_file);
					break;
				case PERF_RECORD_UNTHROTTLE:
					//display_freq(0, hw, options.output_file);
					break;
				default:
					// Skips different entire map page or partial
					size_t to_skip = (ehdr.size == 0) ? map_size : ehdr.size - sizeof(ehdr);

					perf_skip_buffer(perf_event_desc.data(), to_skip);

					++unknown_samples;

					break;
			}
		}

		if (verbose::print_with_lvl(verbose::LVL_MAX)) {
			std::cout << "Filtered samples: " << samples_list.size() << ". Discarded: " << discarded << '\n';
		}
	}

	auto read_samples() -> std::vector<samples::pebs> {
		static constexpr int POLL_TIMEOUT = 0;

		static size_t VECTOR_RESERVE_SIZE = 0;

		const auto ret = poll(poll_fds.data(), num_buffers, POLL_TIMEOUT);

		if (std::cmp_less(ret, 0) && std::cmp_equal(errno, EINTR)) { return {}; }

		std::vector<samples::pebs> samples;
		samples.reserve(VECTOR_RESERVE_SIZE);

		// Read buffers
		for (const auto & group : groups) {
			for (const auto & cpu : system_info::cpus()) {
				process_sample_buf(cpu, all_fds.at(group).at(cpu), sample_type_t(group), samples);
				++buffer_reads.at(group);
			}
		}

		// Recalculate next reserve size
		VECTOR_RESERVE_SIZE = std::max(VECTOR_RESERVE_SIZE, samples.size());

		if (NUM_FAILURES > MAX_FAILURES_BEFORE_REBOOT) { emergency_reboot(); }

		return samples;
	}

	void end() {
		pfm_terminate();

		for (const auto & group : groups) {
			for (const auto & cpu : system_info::cpus()) {
				const auto & fds = all_fds.at(group).at(cpu);
				if (!fds.empty()) {
					for (const auto & fd : fds) {
						close(fd.fd);
					}
					munmap(fds[0].buf, map_size);
					perf_free_fds(fds.data(), static_cast<int>(fds.size()));
				}
			}
		}

		if (verbose::print_with_lvl(verbose::LVL1)) {
			for (const auto & group : groups) {
				const auto * const group_name = to_str(static_cast<sample_type_t>(group));

				std::cout << collected_samples_group.at(group) << " (" << processed_samples_group.at(group) << ") "
				          << group_name << " samples collected (processed) in total " << buffer_reads.at(group)
				          << " poll events and " << lost_samples_group.at(group) << " lost samples" << '\n';
			}
			std::cout << unknown_samples << " unknown samples." << '\n';
			std::cout << discarded_samples << " discarded samples." << '\n';
		}
	}

	void update_freqs() {
		for (const auto & group : groups) {
			for (const auto & cpu : system_info::cpus()) {
				auto & fds = all_fds.at(group).at(cpu)[0];

				fds.hw.sample_freq = freqs.at(group);
			}
		}
	}

} // namespace samples