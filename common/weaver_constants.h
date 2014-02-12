/*
 * ===============================================================
 *    Description:  Constant values used across the project
 *
 *        Created:  01/15/2013 03:01:04 PM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#include <stdint.h>

// debugging
#ifndef WDEBUG
#ifdef __WEAVER_DEBUG__
#define WDEBUG std::cerr << __FILE__ << ":" << __LINE__ << " "
#else
#define WDEBUG if (0) std::cerr << __FILE__ << ":" << __LINE__ << " "
#endif
#endif

#ifndef __CONSTANTS__
#define __CONSTANTS__

// unused expression for no warnings
#define UNUSED(exp) do { (void)(exp); } while (0)

#include "stdint.h"

#define MAX_TIME UINT64_MAX
#define MAX_UINT64 UINT64_MAX
// messaging constants
#define ID_INCR (1ULL << 32ULL)
#define COORD_ID (0ULL)
#define COORD_SM_ID (0ULL)
#define CLIENT_ID (100ULL)
#define SHARDS_FILE "../common/shards"
#define CLIENT_SHARDS_FILE "shards"
// weaver setup
#define NUM_SHARDS 2
#define NUM_VTS 1
#define SHARD_ID_INCR NUM_VTS
#define NUM_THREADS (sysconf(_SC_NPROCESSORS_ONLN ))
#define ID_BITS 8
#define TOP_MASK (0x0fffffffffffffffULL)
#define GRAPH_FILE "graph.rec"
// node programs
#define BATCH_MSG_SIZE 1 // 1 == no batching
#define MAX_CACHE_ENTRIES 8 // 0 to turn off caching
// migration
#define START_MIGR_ID SHARD_ID_INCR // first shard to get migration token
#define SHARD_CAP (360000ULL/NUM_SHARDS)
//#define WEAVER_CLDG // defined if communication-based LDG, false otherwise
//#define WEAVER_MSG_COUNT // defined if client msg count call will take place
// coordinator
#define VT_NANO (1000000000ULL)
#define VT_TIMEOUT_MILL 1 // epoll and nop timeout in ms
#define VT_TIMEOUT_MICR 100 // number of microseconds between successive nops
#define VT_TIMEOUT_NANO 100000 // number of nanoseconds between successive nops
#define VT_INITIAL_TIMEOUT_MILL 5000 // number of milliseconds delay for first nop - setup time for all timestampers
#define VT_INITIAL_TIMEOUT_MICR 5000000 // number of microseconds delay for first nop - setup time for all timestampers
#define VT_INITIAL_TIMEOUT_NANO 5000000000 // number of nanoseconds delay for first nop - setup time for all timestampers
// hyperdex
#define HYPERDEX_COORD_IPADDR "127.0.0.1"
//#define HYPERDEX_COORD_IPADDR "128.84.227.101"
#define HYPERDEX_COORD_PORT 1982
// kronos
#define KRONOS_IPADDR "127.0.0.1"
//#define KRONOS_IPADDR "128.84.227.101"
#define KRONOS_PORT 1992

#endif
