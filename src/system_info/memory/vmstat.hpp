/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_VMSTAT_HPP
#define THANOS_VMSTAT_HPP

#include <cstdint> // for uint64_t
#include <fstream> // for ifstream, ostream, basic_istream
#include <map>     // for map
#include <string>  // for string, operator>>

#include "utils/types.hpp" // for hres_clock, time_point

template<class T = uint64_t>
class vmstat_t {
public:
	static constexpr const char * VMSTAT_FILE = "/proc/vmstat";

private:
	umap<std::string, T> param_val_map_ = {};

	time_point last_update;

public:
	static constexpr const char * nr_free_pages               = "nr_free_pages";
	static constexpr const char * nr_alloc_batch              = "nr_alloc_batch";
	static constexpr const char * nr_inactive_anon            = "nr_inactive_anon";
	static constexpr const char * nr_active_anon              = "nr_active_anon";
	static constexpr const char * nr_inactive_file            = "nr_inactive_file";
	static constexpr const char * nr_active_file              = "nr_active_file";
	static constexpr const char * nr_unevictable              = "nr_unevictable";
	static constexpr const char * nr_mlock                    = "nr_mlock";
	static constexpr const char * nr_anon_pages               = "nr_anon_pages";
	static constexpr const char * nr_mapped                   = "nr_mapped";
	static constexpr const char * nr_file_pages               = "nr_file_pages";
	static constexpr const char * nr_dirty                    = "nr_dirty";
	static constexpr const char * nr_writeback                = "nr_writeback";
	static constexpr const char * nr_slab_reclaimable         = "nr_slab_reclaimable";
	static constexpr const char * nr_slab_unreclaimable       = "nr_slab_unreclaimable";
	static constexpr const char * nr_page_table_pages         = "nr_page_table_pages";
	static constexpr const char * nr_kernel_stack             = "nr_kernel_stack";
	static constexpr const char * nr_unstable                 = "nr_unstable";
	static constexpr const char * nr_bounce                   = "nr_bounce";
	static constexpr const char * nr_vmscan_write             = "nr_vmscan_write";
	static constexpr const char * nr_vmscan_immediate_reclaim = "nr_vmscan_immediate_reclaim";
	static constexpr const char * nr_writeback_temp           = "nr_writeback_temp";
	static constexpr const char * nr_isolated_anon            = "nr_isolated_anon";
	static constexpr const char * nr_isolated_file            = "nr_isolated_file";
	static constexpr const char * nr_shmem                    = "nr_shmem";
	static constexpr const char * nr_dirtied                  = "nr_dirtied";
	static constexpr const char * nr_written                  = "nr_written";
	static constexpr const char * nr_pages_scanned            = "nr_pages_scanned";

	static constexpr const char * numa_hit        = "numa_hit";
	static constexpr const char * numa_miss       = "numa_miss";
	static constexpr const char * numa_foreign    = "numa_foreign";
	static constexpr const char * numa_interleave = "numa_interleave";
	static constexpr const char * numa_local      = "numa_local";
	static constexpr const char * numa_other      = "numa_other";

	static constexpr const char * workingset_refault     = "workingset_refault";
	static constexpr const char * workingset_activate    = "workingset_activate";
	static constexpr const char * workingset_nodereclaim = "workingset_nodereclaim";

	static constexpr const char * nr_anon_transparent_hugepages = "nr_anon_transparent_hugepages";
	static constexpr const char * nr_free_cma                   = "nr_free_cma";
	static constexpr const char * nr_dirty_threshold            = "nr_dirty_threshold";
	static constexpr const char * nr_dirty_background_threshold = "nr_dirty_background_threshold";

	static constexpr const char * pgpgin  = "pgpgin";
	static constexpr const char * pgpgout = "pgpgout";

	static constexpr const char * pswpin  = "pswpin";
	static constexpr const char * pswpout = "pswpout";

	static constexpr const char * pgalloc_dma     = "pgalloc_dma";
	static constexpr const char * pgalloc_dma32   = "pgalloc_dma32";
	static constexpr const char * pgalloc_normal  = "pgalloc_normal";
	static constexpr const char * pgalloc_high    = "pgalloc_high";
	static constexpr const char * pgalloc_movable = "pgalloc_movable";

	static constexpr const char * pgfree       = "pgfree";
	static constexpr const char * pgactivate   = "pgactivate";
	static constexpr const char * pgdeactivate = "pgdeactivate";
	static constexpr const char * pgfault      = "pgfault";
	static constexpr const char * pgmajfault   = "pgmajfault";

	static constexpr const char * pgrefill_dma     = "pgrefill_dma";
	static constexpr const char * pgrefill_dma32   = "pgrefill_dma32";
	static constexpr const char * pgrefill_normal  = "pgrefill_normal";
	static constexpr const char * pgrefill_high    = "pgrefill_high";
	static constexpr const char * pgrefill_movable = "pgrefill_movable";

	static constexpr const char * pgsteal_kswapd_dma     = "pgsteal_kswapd_dma";
	static constexpr const char * pgsteal_kswapd_dma32   = "pgsteal_kswapd_dma32";
	static constexpr const char * pgsteal_kswapd_normal  = "pgsteal_kswapd_normal";
	static constexpr const char * pgsteal_kswapd_high    = "pgsteal_kswapd_high";
	static constexpr const char * pgsteal_kswapd_movable = "pgsteal_kswapd_movable";

	static constexpr const char * pgsteal_direct_dma     = "pgsteal_direct_dma";
	static constexpr const char * pgsteal_direct_dma32   = "pgsteal_direct_dma32";
	static constexpr const char * pgsteal_direct_normal  = "pgsteal_direct_normal";
	static constexpr const char * pgsteal_direct_high    = "pgsteal_direct_high";
	static constexpr const char * pgsteal_direct_movable = "pgsteal_direct_movable";

	static constexpr const char * pgscan_kswapd_dma     = "pgscan_kswapd_dma";
	static constexpr const char * pgscan_kswapd_dma32   = "pgscan_kswapd_dma32";
	static constexpr const char * pgscan_kswapd_normal  = "pgscan_kswapd_normal";
	static constexpr const char * pgscan_kswapd_high    = "pgscan_kswapd_high";
	static constexpr const char * pgscan_kswapd_movable = "pgscan_kswapd_movable";

	static constexpr const char * pgscan_direct_dma      = "pgscan_direct_dma";
	static constexpr const char * pgscan_direct_dma32    = "pgscan_direct_dma32";
	static constexpr const char * pgscan_direct_normal   = "pgscan_direct_normal";
	static constexpr const char * pgscan_direct_high     = "pgscan_direct_high";
	static constexpr const char * pgscan_direct_movable  = "pgscan_direct_movable";
	static constexpr const char * pgscan_direct_throttle = "pgscan_direct_throttle";

	static constexpr const char * zone_reclaim_failed = "zone_reclaim_failed";

	static constexpr const char * pginodesteal = "pginodesteal";

	static constexpr const char * slabs_scanned = "slabs_scanned";

	static constexpr const char * kswapd_inodesteal             = "kswapd_inodesteal";
	static constexpr const char * kswapd_low_wmark_hit_quickly  = "kswapd_low_wmark_hit_quickly";
	static constexpr const char * kswapd_high_wmark_hit_quickly = "kswapd_high_wmark_hit_quickly";

	static constexpr const char * pageoutrun = "pageoutrun";
	static constexpr const char * allocstall = "allocstall";
	static constexpr const char * pgrotated  = "pgrotated";

	static constexpr const char * drop_pagecache = "drop_pagecache";
	static constexpr const char * drop_slab      = "drop_slab";

	static constexpr const char * numa_pte_updates       = "numa_pte_updates";
	static constexpr const char * numa_huge_pte_updates  = "numa_huge_pte_updates";
	static constexpr const char * numa_hint_faults       = "numa_hint_faults";
	static constexpr const char * numa_hint_faults_local = "numa_hint_faults_local";
	static constexpr const char * numa_pages_migrated    = "numa_pages_migrated";

	static constexpr const char * pgmigrate_success = "pgmigrate_success";
	static constexpr const char * pgmigrate_fail    = "pgmigrate_fail";

	static constexpr const char * compact_migrate_scanned = "compact_migrate_scanned";
	static constexpr const char * compact_free_scanned    = "compact_free_scanned";
	static constexpr const char * compact_isolated        = "compact_isolated";
	static constexpr const char * compact_stall           = "compact_stall";
	static constexpr const char * compact_fail            = "compact_fail";
	static constexpr const char * compact_success         = "compact_success";

	static constexpr const char * htlb_buddy_alloc_success = "htlb_buddy_alloc_success";
	static constexpr const char * htlb_buddy_alloc_fail    = "htlb_buddy_alloc_fail";

	static constexpr const char * unevictable_pgs_culled    = "unevictable_pgs_culled";
	static constexpr const char * unevictable_pgs_scanned   = "unevictable_pgs_scanned";
	static constexpr const char * unevictable_pgs_rescued   = "unevictable_pgs_rescued";
	static constexpr const char * unevictable_pgs_mlocked   = "unevictable_pgs_mlocked";
	static constexpr const char * unevictable_pgs_munlocked = "unevictable_pgs_munlocked";
	static constexpr const char * unevictable_pgs_cleared   = "unevictable_pgs_cleared";
	static constexpr const char * unevictable_pgs_stranded  = "unevictable_pgs_stranded";

	static constexpr const char * thp_fault_alloc            = "thp_fault_alloc";
	static constexpr const char * thp_fault_fallback         = "thp_fault_fallback";
	static constexpr const char * thp_collapse_alloc         = "thp_collapse_alloc";
	static constexpr const char * thp_collapse_alloc_failed  = "thp_collapse_alloc_failed";
	static constexpr const char * thp_split                  = "thp_split";
	static constexpr const char * thp_zero_page_alloc        = "thp_zero_page_alloc";
	static constexpr const char * thp_zero_page_alloc_failed = "thp_zero_page_alloc_failed";

	static constexpr const char * balloon_inflate = "balloon_inflate";
	static constexpr const char * balloon_deflate = "balloon_deflate";
	static constexpr const char * balloon_migrate = "balloon_migrate";

	static constexpr const char * nr_tlb_remote_flush          = "nr_tlb_remote_flush";
	static constexpr const char * nr_tlb_remote_flush_received = "nr_tlb_remote_flush_received";
	static constexpr const char * nr_tlb_local_flush_all       = "nr_tlb_local_flush_all";
	static constexpr const char * nr_tlb_local_flush_one       = "nr_tlb_local_flush_one";

	static constexpr const char * vmacache_find_calls   = "vmacache_find_calls";
	static constexpr const char * vmacache_find_hits    = "vmacache_find_hits";
	static constexpr const char * vmacache_full_flushes = "vmacache_full_flushes";

	inline auto update() -> bool {
		std::ifstream file(VMSTAT_FILE);

		if (!file.good()) { return false; }

		last_update = hres_clock::now();

		while (file.good()) {
			std::string param;
			T           value;

			file >> param;
			file >> value;

			param_val_map_[param] = value;
		}

		return true;
	}

	vmstat_t() noexcept : last_update(hres_clock::now()) {
		update();
	}

	[[nodiscard]] inline auto get_value(const std::string & param) const {
		return param_val_map_.at(param);
	}

	inline friend auto operator<<(std::ostream & os, const vmstat_t & vmstat) -> std::ostream & {
		for (const auto & [param, value] : vmstat.param_val_map_) {
			os << param << ' ' << value << '\n';
		}

		return os;
	}
};

#endif /* end of include guard: THANOS_VMSTAT_HPP */
