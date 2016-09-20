/*
 * ===============================================================
 *    Description:  Repeating pattern of graph updates with
 *                  interspersed reachability requests
 *
 *        Created:  04/30/2013 02:10:39 PM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#include <thread>
#include <po6/threads/mutex.h>
#include <po6/threads/cond.h>
 
#include "client/client.h"
#include "node_prog/node_prog_type.h"
#include "node_prog/reach_program.h"
#include "test_base.h"

#define RRP_ITERATIONS 3000

static uint64_t repetitive_nodes[10];
static uint64_t repetitive_edges[10];
static bool check_reachable = false;
static bool end_program = false;
static po6::threads::mutex synch_mutex;
static po6::threads::cond synch_cond(&synch_mutex);
static int n1,n2,n3,n4;
static node_prog::reach_params rp;

void
check_reachability()
{
    client c(CLIENT_ID+1);
    int i, j;
    std::vector<std::pair<uint64_t, node_prog::reach_params>> initial_args;
    while (true) {
        synch_mutex.lock();
        while (!check_reachable && !end_program) {
            synch_cond.wait();
        }
        if (end_program) {
            synch_mutex.unlock();
            return;
        }
        initial_args.clear();
        initial_args.emplace_back(std::make_pair(0, rp));
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                if (i==j) {
                    continue;
                }
                initial_args[0].first = repetitive_nodes[i];
                node_prog::reach_params &params = initial_args[0].second;
                params.prev_node.loc = COORD_ID;
                params.dest = repetitive_nodes[j];
                std::unique_ptr<node_prog::reach_params> res = c.run_node_program(node_prog::REACHABILITY, initial_args);
                if ((i==n1 && j==n2) || (i==n3 && j==n4)) {
                    assert(res->reachable);
                } else {
                    assert(!res->reachable);
                }
            }
        }
        check_reachable = false;
        synch_cond.signal();
        synch_mutex.unlock();
    }
}

void prep_params(int num1, int num2, int num3, int num4)
{
    n1 = num1;
    n2 = num2;
    n3 = num3;
    n4 = num4;
    rp.mode = false;
    rp.reachable = false;
    rp.edge_props.clear();
    check_reachable = true;
}

void
signal_reachable(int num1, int num2, int num3, int num4, std::vector<common::property> &eprops)
{
    synch_mutex.lock();
    prep_params(num1, num2, num3, num4);
    rp.edge_props = eprops;
    synch_cond.signal();
    while (check_reachable && !end_program) {
        synch_cond.wait();
    }
    synch_mutex.unlock();
}

void
signal_reachable(int num1, int num2, int num3, int num4)
{
    synch_mutex.lock();
    prep_params(num1, num2, num3, num4);
    synch_cond.signal();
    while (check_reachable && !end_program) {
        synch_cond.wait();
    }
    synch_mutex.unlock();
}

void 
delete_edges(client *c, int num1, int num2)
{
    c->delete_edge(repetitive_nodes[num1], repetitive_edges[0]);
    c->delete_edge(repetitive_nodes[num2], repetitive_edges[1]);
}

void
create_edges(client *c, int num1, int num2, int num3, int num4)
{
    repetitive_edges[0] = c->create_edge(repetitive_nodes[num1], repetitive_nodes[num2]);
    repetitive_edges[1] = c->create_edge(repetitive_nodes[num3], repetitive_nodes[num4]);
}

void
repetitive_reach_prog(bool to_exit)
{
    client c(CLIENT_ID);
    int i;
    std::thread *t;
    std::vector<common::property> edge_props;
    for (i = 0; i < 10; i++) {
        WDEBUG << "Creating node " << (i+1) << std::endl;
        repetitive_nodes[i] = c.create_node();
    }
    WDEBUG << "Created nodes\n";
    t = new std::thread(check_reachability);
    t->detach();
    
    for (i = 0; i < RRP_ITERATIONS; i++) {
        create_edges(&c,0,1,2,3);
        common::property prop(42, 84, 0);
        c.add_edge_prop(repetitive_nodes[0], repetitive_edges[0], prop.key, prop.value);
        c.add_edge_prop(repetitive_nodes[2], repetitive_edges[1], prop.key, prop.value);
        edge_props.push_back(prop);
        signal_reachable(0,1,2,3, edge_props);
        for (int cnt = 0; cnt < 10; cnt++) {
            signal_reachable(0,1,2,3);
            signal_reachable(0,1,2,3, edge_props);
        }
        delete_edges(&c,0,2);
        edge_props.clear();
        signal_reachable(-1,-1,-1,-1); // nothing reachable
        create_edges(&c,0,3,2,1);
        signal_reachable(0,3,2,1);
        delete_edges(&c,0,2);
        create_edges(&c,0,3,2,1);
        signal_reachable(0,3,2,1);
        delete_edges(&c,0,2);
        create_edges(&c,0,3,2,1);
        delete_edges(&c,0,2);
        create_edges(&c,0,1,2,3);
        signal_reachable(0,1,2,3);
        delete_edges(&c,0,2);
        signal_reachable(-1,-1,-1,-1); // nothing reachable
    }
    for (i = 0; i < 10; i++) {
        c.delete_node(repetitive_nodes[i]);
    }
    // releasing locks, killing all threads
    end_program = true;
    synch_cond.broadcast();
    if (to_exit)
        c.exit_weaver();
}
