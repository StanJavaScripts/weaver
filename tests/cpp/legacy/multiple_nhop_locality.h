/*
 * ===============================================================
 *    Description:  Small set of n hop reachability requests
 *                  issued repeatedly.
 *
 *        Created:  09/22/2013 12:24:06 PM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#include "common/clock.h"
#include "client/client.h"
#include "node_prog/node_prog_type.h"
#include "node_prog/reach_program.h"
#include "test_base.h"

#define ML_NHOP_REQUESTS 200
#define NUM_NHOP_PAIRS 10
#define MAX_HOP_COUNT 100

std::vector<std::pair<uint64_t, uint64_t>>
find_n_hop(test_graph &g, uint64_t hop_count, int num_pairs)
{
    int first, second, i;
    node_prog::reach_params rp;
    std::unique_ptr<node_prog::reach_params> res;
    std::vector<std::pair<uint64_t, uint64_t>> ret;
    rp.mode = false;
    rp.reachable = false;
    rp.prev_node.loc = COORD_ID;
    rp.hops = 0;
    i = 0;
    while (true) {
        first = rand() % g.num_nodes;
        second = rand() % g.num_nodes;
        while (second == first) {
            second = rand() % g.num_nodes;
        }
        std::vector<std::pair<uint64_t, node_prog::reach_params>> initial_args;
        rp.dest = g.nodes[second];
        initial_args.emplace_back(std::make_pair(g.nodes[first], rp));
        res = g.c->run_node_program(node_prog::REACHABILITY, initial_args);
        WDEBUG << "Done request " << ++i << " of initial src-dest search" << std::endl;
        if (res->hops < hop_count && res->hops > 0 && res->reachable) {
            ret.emplace_back(std::make_pair(first, second));
            if (--num_pairs == 0) {
                break;
            }
        }
    }
    WDEBUG << "Going to start n hop locality" << std::endl;
    return ret;
}

// parameter 'dense' decides if the graph is dense (true) or sparse (false)
void
multiple_nhop_locality_prog(bool dense, bool to_exit)
{
    client::client c(CLIENT_ID, 0);
    int i, num_nodes, num_edges;
    uint64_t seed = time(NULL);

    // creating graph
    std::ifstream count_in;
    count_in.open("node_count.rec");
    count_in >> num_nodes;
    count_in.close();
    if (dense) {
        num_edges = (int)(10 * (double)num_nodes);
    } else {
        num_edges = (int)(1.5 * (double)num_nodes);
    }
    test_graph g(&c, seed, num_nodes, num_edges, false, to_exit);

    // find a suitable src-dest pair which has a long(ish) path
    auto pairs = find_n_hop(g, MAX_HOP_COUNT, NUM_NHOP_PAIRS);
    WDEBUG << "Got " << pairs.size() << " pairs\n";
    assert(pairs.size() == NUM_NHOP_PAIRS);
    node_prog::reach_params rp;
    rp.mode = false;
    rp.reachable = false;
    rp.prev_node.loc = COORD_ID;
    rp.hops = 0;
    
    // enable migration now
    //c.start_migration();

    // repeatedly perform same set of requests
    std::ofstream file, req_time;
    //file.open("requests.rec");
    req_time.open("time_weaver.rec");
    uint64_t start, cur, prev, diff;
    wclock::weaver_timer timer;
    start = timer.get_time_elapsed();
    prev = start;
    for (i = 0; i < ML_REQUESTS; i++) {
        cur = timer.get_time_elapsed();
        diff = cur - prev;
        WDEBUG << "Test: i = " << i << ", " << diff << std::endl;
        req_time << diff << std::endl;
        //if (i % 10 == 0) {
        //    diff = cur - start;
        //    req_time << diff << std::endl;
        //}
        prev = cur;
        for (auto &p: pairs) {
            rp.dest = g.nodes[p.second];
            std::vector<std::pair<uint64_t, node_prog::reach_params>> initial_args;
            initial_args.emplace_back(std::make_pair(g.nodes[p.first], rp));
            std::unique_ptr<node_prog::reach_params> res = c.run_node_program(node_prog::REACHABILITY, initial_args);
            assert(res->reachable);
        }
    }
    //file.close();
    req_time.close();
    diff = cur - start;
    WDEBUG << "Total time taken " << diff << std::endl;
    //std::ofstream stat_file;
    //stat_file.open("stats.rec", std::ios::out | std::ios::app);
    //stat_file << num_nodes << " " << dif.tv_sec << "." << dif.tv_nsec << std::endl;
    //stat_file.close();
    //g.end_test();
}

void
multiple_nhop_sparse_locality(bool to_exit)
{
    multiple_nhop_locality_prog(false, to_exit);
}

void
multiple_nhop_dense_locality(bool to_exit)
{
    multiple_nhop_locality_prog(true, to_exit);
}
