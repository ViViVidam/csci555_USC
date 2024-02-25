/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <r.laso@usc.es> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Ruben Laso
 * ----------------------------------------------------------------------------
 */

#ifndef THANOS_TYPES_HPP
#define THANOS_TYPES_HPP

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <cstdint> // for int64_t, uint64_t

#include <chrono> // for high_resolution_clock

using time_point = std::chrono::high_resolution_clock::time_point;
using hres_clock = std::chrono::high_resolution_clock;

// pid_t is already defined in Linux as an int
using cpu_t  = int;      // CPU type
using tim_t  = int64_t;  // Timestamp type (nanoseconds)
using ins_t  = int64_t;  // Number of instructions
using lat_t  = int64_t;  // Latency measure type (milliseconds)
using req_t  = int64_t;  // Number of memory requests
using node_t = int;      // NUMA node
using addr_t = uint64_t; // Address type (cannot be "void *" because of binary operations)
using dsrc_t = uint64_t; // Data source
using real_t = float;    // Type for real numbers (to change easy between float and double)

template<typename... args>
using umap = std::unordered_map<args...>;

template<typename... args>
using fast_umap = std::unordered_map<args...>;

template<typename... args>
using map = std::map<args...>;

template<typename... args>
using uset = std::unordered_set<args...>;

template<typename... args>
using fast_uset = std::unordered_set<args...>;

template<typename... args>
using set = std::set<args...>;

#endif /* end of include guard: THANOS_TYPES_HPP */
