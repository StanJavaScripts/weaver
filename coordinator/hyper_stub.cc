/*
 * ===============================================================
 *    Description:  Implementation of hyperdex timestamper stub.
 *
 *        Created:  2014-02-27 15:18:59
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013-2014, Cornell University, see the LICENSE
 *                     file for licensing agreement
 * ===============================================================
 */

#include <e/buffer.h>

#define weaver_debug_
#include "common/config_constants.h"
#include "common/weaver_constants.h"
#include "common/event_order.h"
#include "coordinator/hyper_stub.h"

using coordinator::hyper_stub;

hyper_stub :: hyper_stub()
    : vt_id(UINT64_MAX)
{ }

void
hyper_stub :: init(uint64_t vtid)
{
    vt_id = vtid;
}

std::unordered_map<node_handle_t, uint64_t>
hyper_stub :: get_mappings(std::unordered_set<node_handle_t> &get_set)
{
    std::unordered_map<node_handle_t, uint64_t> ret;

    if (get_set.size() == 1) {
        node_handle_t h = *get_set.begin();
        uint64_t loc = get_nmap(h);
        if (loc != UINT64_MAX) {
            ret.emplace(h, loc);
        } else {
            ret.clear();
        }
    } else if (!get_set.empty()) {
        ret = get_nmap(get_set, false);
    }

    return ret;
}

bool
hyper_stub :: get_idx(std::unordered_map<std::string, std::pair<std::string, uint64_t>> &idx_map)
{
    return get_indices(idx_map, false);
}

void
hyper_stub :: clean_node(db::node *n)
{
    for (auto &x: n->out_edges) {
        for (db::edge *e: x.second) {
            delete e;
        }
    }
    n->out_edges.clear();
    delete n;
}

void
hyper_stub :: clean_up(std::unordered_map<node_handle_t, db::node*> &nodes)
{
    for (auto &p: nodes) {
        clean_node(p.second);
    }
    nodes.clear();
}

void
hyper_stub :: clean_up(std::vector<db::node*> &nodes)
{
    for (auto n: nodes) {
        clean_node(n);
    }
    nodes.clear();
}

void
do_tx(std::unordered_set<node_handle_t> &get_nodes,
      std::unordered_set<node_handle_t> &get_aliases,
      std::unordered_set<edge_handle_t> &get_edges,
      std::unordered_map<node_handle_t, std::vector<uint64_t>> &create_nodes,
      std::unordered_set<edge_handle_t> &create_edges,
      std::shared_ptr<transaction::pending_tx> tx,
      bool &ready,
      bool &error,
      order::oracle *time_oracle)
{
    std::unordered_map<std::string, std::pair<node_handle_t, uint64_t>> indices;
    if (AuxIndex) {
        indices.reserve(get_aliases.size());
        std::pair<node_handle_t, uint64_t> empty_pair;
        for (const std::string &i: idx_get_set) {
            if (idx_add.find(i) == idx_add.end()) {
                indices.emplace(i, empty_pair);
            }
        }
        if (!indices.empty()) {
            if (!get_indices(indices, true)) {
                WDEBUG << "get_indices" << std::endl;
                ERROR_FAIL;
            }
        }

        for (const auto &p: indices) {
            if (put_map.find(p.second.first) == put_map.end()
             && get_set.find(p.second.first) == get_set.end()) {
                get_set.emplace(p.second.first);
            }

            if (del_set.find(p.first) != del_set.end()) {
                del_set.erase(p.first);
                del_set.emplace(p.second.first);
            }
        }
    }


    /*
#define ERROR_FAIL \
    error = true; \
    abort_tx(); \
    clean_up(old_nodes); \
    clean_up(new_nodes); \
    clean_up(del_vec); \
    return;

    ready = false;
    error = false;

    std::unordered_map<node_handle_t, db::node*> old_nodes, new_nodes;
    std::vector<db::node*> del_vec;
    begin_tx();

    // get aux indices from HyperDex
    std::unordered_map<std::string, std::pair<node_handle_t, uint64_t>> indices;
    if (AuxIndex) {
        indices.reserve(idx_get_set.size());
        std::pair<node_handle_t, uint64_t> empty_pair;
        for (const std::string &i: idx_get_set) {
            if (idx_add.find(i) == idx_add.end()) {
                indices.emplace(i, empty_pair);
            }
        }
        if (!indices.empty()) {
            if (!get_indices(indices, true)) {
                WDEBUG << "get_indices" << std::endl;
                ERROR_FAIL;
            }
        }

        for (const auto &p: indices) {
            if (put_map.find(p.second.first) == put_map.end()
             && get_set.find(p.second.first) == get_set.end()) {
                get_set.emplace(p.second.first);
            }

            if (del_set.find(p.first) != del_set.end()) {
                del_set.erase(p.first);
                del_set.emplace(p.second.first);
            }
        }
    }

    // get all nodes from Hyperdex (we need at least last upd clk)
    for (const node_handle_t &h: get_set) {
        if (put_map.find(h) != put_map.end()) {
            WDEBUG << "logical error, get node already in put map " << h << std::endl;
            ERROR_FAIL;
        }
        old_nodes[h] = new db::node(h, UINT64_MAX, dummy_clk, &dummy_mtx);
    }
    for (const node_handle_t &h: del_set) {
        if (old_nodes.find(h) == old_nodes.end()) {
            old_nodes[h] = new db::node(h, UINT64_MAX, dummy_clk, &dummy_mtx);
        }
    }
    if (!get_nodes(old_nodes, true)) {
        WDEBUG << "get nodes" << std::endl;
        ERROR_FAIL;
    }

    // last upd clk check
    std::vector<vc::vclock> before;
    before.reserve(old_nodes.size());
    for (const auto &p: old_nodes) {
        before.emplace_back(*p.second->last_upd_clk);
    }
    if (!time_oracle->assign_vt_order(before, tx->timestamp)) {
        // will retry with higher timestamp
        abort_tx();
        return;
    }

    vc::vclock_ptr_t tx_clk_ptr(new vc::vclock(tx->timestamp));
    for (const auto &p: put_map) {
        new_nodes[p.first] = new db::node(p.first, p.second, tx_clk_ptr, &dummy_mtx);
        new_nodes[p.first]->last_upd_clk.reset(new vc::vclock(*tx_clk_ptr));
        new_nodes[p.first]->restore_clk.reset(new vc::vclock_t(tx_clk_ptr->clock));
    }

#define CHECK_LOC(loc, handle, alias) \
    if (loc == UINT64_MAX) { \
        if (handle != "") { \
            node_iter = old_nodes.find(handle); \
            if (node_iter == old_nodes.end()) { \
                node_iter = new_nodes.find(handle); \
                if (node_iter == new_nodes.end()) { \
                    WDEBUG << "check loc, node = " << handle << std::endl; \
                    ERROR_FAIL; \
                } \
            } \
            loc = node_iter->second->shard; \
        } else { \
            idx_get_iter = indices.find(alias); \
            if (idx_get_iter == indices.end()) { \
                WDEBUG << "check loc, alias = " << alias << std::endl; \
                ERROR_FAIL; \
            } else { \
                handle = idx_get_iter->second.first; \
                loc = idx_get_iter->second.second; \
            } \
        } \
    }

#define GET_NODE(handle) \
    node_iter = old_nodes.find(handle); \
    if (node_iter == old_nodes.end()) { \
        node_iter = new_nodes.find(handle); \
        if (node_iter == new_nodes.end()) { \
            WDEBUG << "get node " << handle << std::endl; \
            ERROR_FAIL; \
        } \
    } \
    n = &(*node_iter->second);

#define CHECK_AUX_INDEX \
    if (AuxIndex) { \
        idx_add_iter = idx_add.find(upd->handle1); \
        if (idx_add_iter != idx_add.end()) { \
            upd->handle2 = idx_add_iter->second->get_handle(); \
        } \
    }

    WDEBUG << "here" << std::endl;
    auto node_iter = old_nodes.end();
    auto idx_get_iter = indices.end();
    auto idx_add_iter = idx_add.end();
    db::node *n = nullptr;
    std::vector<std::string> idx_del;
    uint64_t num_edges_put = 0;
    db::data_map<std::vector<db::edge*>> edges_to_put;

    for (std::shared_ptr<transaction::pending_update> upd: tx->writes) {
        switch (upd->type) {
            case transaction::NODE_CREATE_REQ:
                if (idx_add.find(upd->handle) != idx_add.end()) {
                    WDEBUG << "cannot add two identical handles " << upd->handle << std::endl;
                    ERROR_FAIL;
                }
                GET_NODE(upd->handle);
                n->max_edge_id = uint64max_dist(mt64_gen);
                idx_add.emplace(upd->handle, n);
                break;

            case transaction::EDGE_CREATE_REQ: {
                CHECK_LOC(upd->loc1, upd->handle1, upd->alias1);
                CHECK_LOC(upd->loc2, upd->handle2, upd->alias2);
                GET_NODE(upd->handle1);

                if (n->out_edges.find(upd->handle) != n->out_edges.end()) {
                    WDEBUG << "edge with handle " << upd->handle << " already exists at node " << upd->handle1 << std::endl;
                    ERROR_FAIL;
                }
                db::edge *e = new db::edge(upd->handle, tx_clk_ptr, upd->loc2, upd->handle2);
                n->add_edge(e);

                hyperdex_client_returncode put_code;
                uint64_t edge_id;
                do {
                    edge_id = n->max_edge_id++;
                    put_code = put_new_edge(edge_id, e);
                    num_edges_put++;
                } while (put_code == HYPERDEX_CLIENT_CMPFAIL);

                if (put_code != HYPERDEX_CLIENT_SUCCESS) {
                    WDEBUG << "error putting new edge, handle=" << upd->handle << " at node " << upd->handle1 << std::endl;
                    ERROR_FAIL;
                }
                WDEBUG << "edge=" << e->get_handle() << " id=" << e->edge_id << std::endl;

                n->edge_ids.emplace(edge_id);

                if (AuxIndex) {
                    if (idx_add.find(upd->handle) == idx_add.end()) {
                        WDEBUG << "logical error: does not exist in idx_add " << upd->handle << std::endl;
                        ERROR_FAIL;
                    }
                    idx_add[upd->handle] = n;
                }
                break;
            }

            case transaction::NODE_DELETE_REQ:
                CHECK_LOC(upd->loc1, upd->handle1, upd->alias1);
                GET_NODE(upd->handle1);

                idx_del.emplace_back(upd->handle1);
                for (const std::string &alias: n->aliases) {
                    idx_del.emplace_back(alias);
                }
                break;

            case transaction::NODE_SET_PROPERTY:
                CHECK_LOC(upd->loc1, upd->handle1, upd->alias1);
                GET_NODE(upd->handle1);

                if (!n->base.set_property(*upd->key, *upd->value, tx_clk_ptr)) {
                    WDEBUG << "set property " << *upd->key << ": " << *upd->value << " fail at node " << upd->handle1 << std::endl;
                    ERROR_FAIL;
                }
                break;

            case transaction::EDGE_DELETE_REQ:
                CHECK_AUX_INDEX;
                if (upd->alias2 != "") {
                    CHECK_LOC(upd->loc1, upd->handle2, upd->alias2);
                } else {
                    // if no alias provided, use edge handle as alias
                    CHECK_LOC(upd->loc1, upd->handle2, upd->handle1);
                }
                GET_NODE(upd->handle2);

                if (n->out_edges.find(upd->handle1) == n->out_edges.end()) {
                    WDEBUG << "edge with handle " << upd->handle1 << " does not exist at node " << upd->handle2 << std::endl;
                    ERROR_FAIL;
                }
                for (db::edge *e: n->out_edges[upd->handle1]) {
                    WDEBUG << "remove id=" << e->edge_id << " for edge=" << e->get_handle() << std::endl;
                    n->edge_ids.erase(e->edge_id);
                    if (!del_edge(e->edge_id)) {
                        WDEBUG << "did not find edge id=" << e->edge_id << ", edge handle=" << upd->handle1
                               << " at node=" << n->get_handle() << std::endl;
                        ERROR_FAIL;
                    }
                    delete e;
                }
                n->out_edges.erase(upd->handle1);

                if (AuxIndex) {
                    idx_del.emplace_back(upd->handle1);
                }
                break;

            case transaction::EDGE_SET_PROPERTY:
                CHECK_AUX_INDEX;
                if (upd->alias2 != "") {
                    CHECK_LOC(upd->loc1, upd->handle2, upd->alias2);
                } else {
                    // if no alias provided, use edge handle as alias
                    CHECK_LOC(upd->loc1, upd->handle2, upd->handle1);
                }
                GET_NODE(upd->handle2);

                if (n->out_edges.find(upd->handle1) == n->out_edges.end()) {
                    WDEBUG << "edge with handle " << upd->handle1 << " does not exist at node " << upd->handle2 << std::endl;
                    ERROR_FAIL;
                }
                if (!n->out_edges[upd->handle1].front()->base.set_property(*upd->key, *upd->value, tx_clk_ptr)) {
                    WDEBUG << "property " << *upd->key << ": " << *upd->value << " fail at edge " << upd->handle1 << std::endl;
                    ERROR_FAIL;
                }
                edges_to_put[upd->handle1] = n->out_edges[upd->handle1];
                num_edges_put++;
                break;

            case transaction::ADD_AUX_INDEX:
                CHECK_LOC(upd->loc1, upd->handle1, upd->alias1);
                GET_NODE(upd->handle1);
                if (idx_add.find(upd->handle) != idx_add.end()) {
                    WDEBUG << "cannot add two identical handles " << upd->handle << std::endl;
                    ERROR_FAIL;
                }
                idx_add.emplace(upd->handle, n);
                n->add_alias(upd->handle);
                break;
        }
        uint64_t idx = upd->loc1-ShardIdIncr;
        if (tx->shard_write.size() < idx+1) {
            tx->shard_write.resize(idx+1, false);
        }
        tx->shard_write[idx] = true;

        if (n != nullptr) {
            assert(n->restore_clk->size() == ClkSz);
            (*n->restore_clk)[vt_id+1] = tx_clk_ptr->get_clock();
        }

        n = nullptr;
    }

#undef CHECK_LOC
#undef GET_NODE

    WDEBUG << "here" << std::endl;
    del_vec.reserve(del_set.size());
    for (const node_handle_t &h: del_set) {
        node_iter = old_nodes.find(h);
        if (node_iter == old_nodes.end()) {
            node_iter = new_nodes.find(h);
            if (node_iter == new_nodes.end()) {
                WDEBUG << "did not find node " << h << std::endl;
                ERROR_FAIL;
            }
            n = node_iter->second;
            new_nodes.erase(h);
        } else {
            n = node_iter->second;
            old_nodes.erase(h);
        }
        del_vec.emplace_back(n);
    }

    uint64_t num_total_edges = 0;
    for (const auto &p: old_nodes) {
        num_total_edges += p.second->out_edges.size();
    }
    for (const auto &p: new_nodes) {
        num_total_edges += p.second->out_edges.size();
    }
    WDEBUG << "HD calls start, #put_nodes=" << (old_nodes.size() + new_nodes.size() + idx_add.size())
           << ", #edges=" << num_edges_put << ", #total_edges=" << num_total_edges
           << ", #dels=" << (del_vec.size() + idx_del.size()) << std::endl;
    if (!put_nodes(old_nodes, false)
     || !put_nodes(new_nodes, true)
     || !put_edges(edges_to_put)
     || !add_indices(idx_add, true, true)
     || !del_nodes(del_vec)
     || !del_indices(idx_del)) {
        WDEBUG << "hyperdex error with put_nodes/put_edges/add_indices/del_nodes/del_indices" << std::endl;
        ERROR_FAIL;
    }
    WDEBUG << "HD calls end" << std::endl;

    hyperdex_client_attribute attr[NUM_TX_ATTRS];
    attr[0].attr = tx_attrs[0];
    attr[0].value = (const char*)&vt_id;
    attr[0].value_sz = sizeof(int64_t);
    attr[0].datatype = tx_dtypes[0];

    uint64_t buf_sz = message::size(*tx);
    std::unique_ptr<e::buffer> buf(e::buffer::create(buf_sz));
    e::packer packer = buf->pack_at(0);
    message::pack_buffer(packer, *tx);

    attr[1].attr = tx_attrs[1];
    attr[1].value = (const char*)buf->data();
    attr[1].value_sz = buf->size();
    attr[1].datatype = tx_dtypes[1];
    WDEBUG << "here" << std::endl;

    if (!call(&hyperdex_client_xact_put, tx_space, (const char*)&tx->id, sizeof(int64_t), attr, NUM_TX_ATTRS)) {
        WDEBUG << "hyperdex tx put error, tx id " << tx->id << std::endl;
        ERROR_FAIL;
    }
    WDEBUG << "here" << std::endl;

    hyperdex_client_returncode commit_status = HYPERDEX_CLIENT_GARBAGE;
    commit_tx(commit_status);
    WDEBUG << "here" << std::endl;

    switch(commit_status) {
        case HYPERDEX_CLIENT_SUCCESS:
            ready = true;
            assert(!error);
            break;

        case HYPERDEX_CLIENT_ABORTED:
            ready = false;
            assert(!error);
            break;

        default:
            error = true;
            ready = false;
    }

    clean_up(old_nodes);
    clean_up(new_nodes);
    clean_up(del_vec);
    WDEBUG << "here" << std::endl;

#undef ERROR_FAIL
    */
}


void
hyper_stub :: clean_tx(uint64_t tx_id)
{
    begin_tx();
    if (del(tx_space, (const char*)&tx_id, sizeof(int64_t))) {
        hyperdex_client_returncode commit_status = HYPERDEX_CLIENT_GARBAGE;
        commit_tx(commit_status);

        switch (commit_status) {
            case HYPERDEX_CLIENT_SUCCESS:
                break;
            
            default:
                WDEBUG << "problem in committing tx for clean_tx, returned status = "
                       << hyperdex_client_returncode_to_string(commit_status) << std::endl;
        }
    } else {
        abort_tx();
    }
}

// status = false if not prepared
void
hyper_stub :: recreate_tx(const hyperdex_client_attribute *cl_attr,
    transaction::pending_tx &tx)
{
    int tx_data_idx;
    if (strncmp(cl_attr[0].attr, tx_attrs[1], 7) == 0) {
        tx_data_idx = 0;
    } else if (strncmp(cl_attr[2].attr, tx_attrs[1], 7) == 0) {
        tx_data_idx = 2;
    } else {
        assert(strncmp(cl_attr[1].attr, tx_attrs[1], 7) == 0);
        tx_data_idx = 1;
    }
    assert(cl_attr[tx_data_idx].datatype == tx_dtypes[1]);

    std::unique_ptr<e::buffer> buf(e::buffer::create(cl_attr[tx_data_idx].value,
                                                     cl_attr[tx_data_idx].value_sz));
    e::unpacker unpacker = buf->unpack_from(0);
    message::unpack_buffer(unpacker, tx);
}

void
hyper_stub :: restore_backup(std::vector<std::shared_ptr<transaction::pending_tx>> &txs)
{
    const hyperdex_client_attribute *cl_attr;
    size_t num_attrs;

    // tx list
    const hyperdex_client_attribute_check attr_check = {tx_attrs[0], (const char*)&vt_id, sizeof(int64_t), tx_dtypes[0], HYPERPREDICATE_EQUALS};
    enum hyperdex_client_returncode search_status, loop_status;

    int64_t call_id = hyperdex_client_search(cl, tx_space, &attr_check, 1, &search_status, &cl_attr, &num_attrs);
    if (call_id < 0) {
        WDEBUG << "Hyperdex function failed, op id = " << call_id
               << ", status = " << hyperdex_client_returncode_to_string(search_status) << std::endl;
        WDEBUG << "error message: " << hyperdex_client_error_message(cl) << std::endl;
        WDEBUG << "error loc: " << hyperdex_client_error_location(cl) << std::endl;
        return;
    }

    int64_t loop_id;
    bool loop_done = false;
    std::shared_ptr<transaction::pending_tx> tx;
    while (!loop_done) {
        // loop until search done
        loop_id = hyperdex_client_loop(cl, -1, &loop_status);
        if (loop_id != call_id
         || loop_status != HYPERDEX_CLIENT_SUCCESS
         || (search_status != HYPERDEX_CLIENT_SUCCESS && search_status != HYPERDEX_CLIENT_SEARCHDONE)) {
            WDEBUG << "Hyperdex function failed, call id = " << call_id
                   << ", loop_id = " << loop_id
                   << ", loop status = " << hyperdex_client_returncode_to_string(loop_status)
                   << ", search status = " << hyperdex_client_returncode_to_string(search_status) << std::endl;
            WDEBUG << "error message: " << hyperdex_client_error_message(cl) << std::endl;
            WDEBUG << "error loc: " << hyperdex_client_error_location(cl) << std::endl;
            return;
        }

        switch (search_status) {
            case HYPERDEX_CLIENT_SEARCHDONE:
            loop_done = true;
            break;

            case HYPERDEX_CLIENT_SUCCESS:
            assert(num_attrs == (NUM_TX_ATTRS+1));

            tx = std::make_shared<transaction::pending_tx>(transaction::UPDATE);
            recreate_tx(cl_attr, *tx);
            txs.emplace_back(tx);
            hyperdex_client_destroy_attrs(cl_attr, num_attrs);
            break;

            default:
            WDEBUG << "unexpected hyperdex client status on search: " << search_status << std::endl;
        }
    }

    WDEBUG << "Got " << txs.size() << " transactions for vt " << vt_id << std::endl;
}
