/*
 * ===============================================================
 *    Description:  Implementation of ordering methods.
 *
 *        Created:  03/19/2014 11:25:02 PM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

//#define weaver_debug_
#include "common/event_order.h"
#include "common/config_constants.h"

using order::oracle;

oracle :: oracle()
    : kronos_cl(chronos_client_create(KronosIpaddr.c_str(), KronosPort))
    , cache_hits(0)
{ }

// static members

// return the smaller of the two clocks
// return -1 if clocks cannot be compared
// return 2 if clocks are identical
int
oracle :: compare_two_clocks(const vc::vclock_t &clk1, const vc::vclock_t &clk2)
{
    int ret = 2;

    uint64_t clk_sz = clk1.size() < clk2.size() ? clk1.size() : clk2.size();
    //assert(clk1.size() == ClkSz);
    //assert(clk2.size() == ClkSz);

    // check epoch number
    if (clk1[0] < clk2[0]) {
        return 0;
    } else if (clk1[0] > clk2[0]) {
        return 1;
    }

    // same epoch number, compare each entry in vector
    for (uint64_t i = 1; i < clk_sz; i++) {
        if ((clk1[i] < clk2[i]) && (ret != 0)) {
            if (ret == 2) {
                ret = 0;
            } else {
                return -1;
            }
        } else if ((clk1[i] > clk2[i]) && (ret != 1)) {
            if (ret == 2) {
                ret = 1;
            } else {
                return -1;
            }
        }
    }

    return ret;
}

// method which only compares vector clocks
// returns bool vector, with i'th entry as true if that clock is definitely larger than some other clock
std::vector<bool>
oracle :: compare_vector_clocks(const std::vector<vc::vclock> &clocks)
{
    uint64_t num_clks = clocks.size();
    std::vector<bool> large(num_clks, false); // keep track of clocks which are definitely not smallest
    uint64_t num_large = 0; // number of true values in previous vector
    for (uint64_t i = 0; i < (num_clks-1); i++) {
        for (uint64_t j = (i+1); j < num_clks; j++) {
            int cmp = compare_two_clocks(clocks.at(i).clock, clocks.at(j).clock);
            assert(cmp != 2);
            if ((cmp == 0) && !large.at(j)) {
                large.at(j) = true;
                num_large++;
            } else if ((cmp == 1) && !large.at(i)) {
                large.at(i) = true;
                num_large++;
            }
        }
        if (num_large == (num_clks-1)) {
            return large;
        }
    }
    assert(num_large < (num_clks-1));
    return large;
}

// return the index of the (only) 'false' in a bool vector
uint64_t
get_false_position(std::vector<bool> &large)
{
    for (uint64_t min_pos = 0; min_pos < large.size(); min_pos++) {
        if (!large[min_pos]) {
            return min_pos;
        }
    }
    assert(false);
    return UINT64_MAX;
}

// plain old vector clock comparison
// if the vector has a single clock which happens before all the others, the index of that clock is stored in small_idx
// large[i] is true if clocks[i] is definitely not the earliest clock
// i.e. large[i] <=> \exists j clock[j] -> clock[i], where -> is the happens before relationship for vector clocks
void
oracle :: compare_vts_no_kronos(const std::vector<vc::vclock> &clocks, std::vector<bool> &large, int64_t &small_idx)
{
    large = compare_vector_clocks(clocks);
    assert(clocks.size() == large.size());

    if (std::count(large.begin(), large.end(), false) == 1) {
        // Kronos not required
        small_idx = get_false_position(large);
    }
}

bool
oracle :: happens_before_no_kronos(const vc::vclock_t &vclk1, const vc::vclock_t &vclk2)
{
    if (compare_two_clocks(vclk1, vclk2) != 0) {
        return false;
    } else {
        return true;
    }
}

bool
oracle :: happens_before_no_kronos(const vc::vclock_t &vclk, const std::vector<vc::vclock_t*> &others)
{
    for (const vc::vclock_t* const other: others) {
        if (compare_two_clocks(vclk, *other) != 0) {
            return false;
        }
    }
    return true;
}

bool
oracle :: equal_or_happens_before_no_kronos(const vc::vclock_t &vclk1, const vc::vclock_t &vclk2)
{
    switch (compare_two_clocks(vclk1, vclk2)) {
        case 0:
        case 2:
        return true;

        default:
        return false;
    }
}


// non-static members which use kronos_cl

// vector clock comparison method
// will call Kronos if clocks are incomparable
// returns index of earliest clock`
int64_t
oracle :: compare_vts(const std::vector<vc::vclock> &clocks)
{
    //return ((int64_t)weaver_util::urandom_uint64() % clocks.size());
    //XXX static uint32_t rev_count = 0;

    std::vector<bool> large;
    int64_t ret_idx = INT64_MAX;

    compare_vts_no_kronos(clocks, large, ret_idx);
    if (ret_idx != INT64_MAX) {
        // Kronos not required
        return ret_idx;
    } else {
        uint64_t num_clks = clocks.size();
        // TODO turning off cache for now
        //// check cache
        //for (uint64_t i = 0; i < num_clks; i++) {
        //    for (uint64_t j = i+1; j < num_clks; j++) {
        //        if (!large.at(i) && !large.at(j)) {
        //            int cmp = kcache.compare(clocks[i].clock, clocks[j].clock);
        //            if (cmp == 0) {
        //                large[j] = true;
        //                cache_hits++;
        //            } else if (cmp == 1) {
        //                large[i] = true;
        //                cache_hits++;
        //            } else {
        //                assert(cmp == -1);
        //            }
        //        }
        //    }
        //}
        uint64_t num_large = std::count(large.begin(), large.end(), true);
        if (num_large == (num_clks-1)) {
            // Kronos not required
            return get_false_position(large);
        }

        // need to call Kronos
        uint64_t num_pairs = ((num_clks - num_large) * (num_clks - num_large - 1)) / 2;
        weaver_pair *wpair = new weaver_pair[num_pairs];
        weaver_pair *wp = wpair;
        uint64_t epoch_num = UINT64_MAX;

        for (uint64_t i = 0; i < num_clks; i++) {
            for (uint64_t j = i+1; j < num_clks; j++) {
                if (!large.at(i) && !large.at(j)) {
                    if (epoch_num == UINT64_MAX) {
                        epoch_num = clocks[i].clock[0];
                        assert(clocks[j].clock[0] == epoch_num);
                    } else {
                        assert(clocks[i].clock[0] == epoch_num);
                        assert(clocks[j].clock[0] == epoch_num);
                    }

                    wp->lhs = clocks[i].clock;
                    wp->rhs = clocks[j].clock;
                    wp->lhs_id = clocks[i].vt_id;
                    wp->rhs_id = clocks[j].vt_id;
                    wp->flags = CHRONOS_SOFT_FAIL;
                    wp->order = CHRONOS_HAPPENS_BEFORE;
                    wp++;
                }
            }
        }

        chronos_returncode status;
        ssize_t cret;

        int64_t ret = kronos_cl->weaver_order(wpair, num_pairs, &status, &cret);
        ret = kronos_cl->wait(ret, 100000, &status);

        wp = wpair;
        std::vector<bool> large_upd = large;
        //XXX std::vector<bool> test_bad(3, false);
        //XXX int bad_count = 0;
        for (uint64_t i = 0; i < num_clks; i++) {
            for (uint64_t j = i+1; j < num_clks; j++) {
                if (!large.at(i) && !large.at(j)) {
                    // retrieve and set order
                    assert((wp->order == CHRONOS_HAPPENS_BEFORE) || (wp->order == CHRONOS_HAPPENS_AFTER));
                    // fine-grained locking for cache.add() so that other requests are not blocked for a long time
                    switch (wp->order) {
                        case CHRONOS_HAPPENS_BEFORE:
                            large_upd.at(j) = true;
                            //TODO turning off cache kcache.add(clocks[i].clock, clocks[j].clock);
                            //XXX if (num_pairs == 3 && (bad_count == 0 || bad_count == 2)) {
                            //XXX     test_bad[bad_count] = true;
                            //XXX }
                            break;

                        case CHRONOS_HAPPENS_AFTER:
                            large_upd.at(i) = true;
                            //TODO turning off cache kcache.add(clocks[j].clock, clocks[i].clock);
                            //XXX if (num_pairs == 3 && bad_count == 1) {
                            //XXX     test_bad[bad_count] = true;
                            //XXX }
                            break;

                        default:
                            WDEBUG << "cannot reach here" << std::endl;
                            assert(false);
                    }
                    wp++;
                    //XXX bad_count++;
                }
            }
        }
        delete[] wpair;

        // XXX
        //if (test_bad[0] && test_bad[1] && test_bad[2]) {
        //    WDEBUG << ++rev_count << std::endl;
        //}

        for (uint64_t min_pos = 0; min_pos < num_clks; min_pos++) {
            if (!large_upd.at(min_pos)) {
                return min_pos;
            }
        }
        // should never reach here
        assert(false);
        return INT64_MAX;
    }
}

// compare two vector clocks
// return 0 if first is smaller, 1 if second is smaller, 2 if identical
int64_t
oracle :: compare_two_vts(const vc::vclock &clk1, const vc::vclock &clk2)
{
    int cmp = compare_two_clocks(clk1.clock, clk2.clock);
    if (cmp == -1) {
        std::vector<vc::vclock> compare_vclks;
        compare_vclks.push_back(clk1);
        compare_vclks.push_back(clk2);
        cmp = compare_vts(compare_vclks);
    }
    return cmp;
}

// return true if the first clock occurred between the second two
// assert not equal because vector clocks are unique.  no two clock are the same in every coordinate of the vector.
bool
oracle :: clock_creat_before_del_after(const vc::vclock &req_vclock,
    const vclock_ptr_t &creat_time,
    const vclock_ptr_t &del_time)
{
    bool cmp = true;

    if (del_time) {
        int64_t cmp_1 = compare_two_vts(*del_time, req_vclock);
        assert(cmp_1 != 2);
        cmp = (cmp_1 == 1);
    }

    if (cmp) {
        int64_t cmp_2 = compare_two_vts(*creat_time, req_vclock);
        assert(cmp_2 != 2);
        cmp = (cmp_2 == 0);
    }

    return cmp;
}

// assign 'after' happens after all 'before'
// will call Kronos if clocks are incomparable
// returns true if successful, false if assignment impossible
bool
oracle :: assign_vt_order(const std::vector<vc::vclock> &before, const vc::vclock &after)
{
    // check if can compare without kronos
    std::vector<uint64_t> need_kronos;
    for (uint64_t i = 0; i < before.size(); i++) {
        int cmp = compare_two_clocks(before[i].clock, after.clock);
        if (cmp >= 1) {
            return false;
        } else if (cmp == -1) {
            need_kronos.emplace_back(i);
        }
    }

    if (need_kronos.empty()) {
        return true;
    }

    // need to call Kronos

    uint64_t num_pairs = need_kronos.size();
    weaver_pair *wpair = new weaver_pair[num_pairs];
    
    // prep kronos args
    for (uint64_t i = 0; i < num_pairs; i++) {
        weaver_pair &wp = wpair[i];
        uint64_t idx = need_kronos[i];
        wp.lhs = before[idx].clock;
        wp.rhs = after.clock;
        wp.lhs_id = before[idx].vt_id;
        wp.rhs_id = after.vt_id;
        wp.flags = 0;
        wp.order = CHRONOS_HAPPENS_BEFORE;
    }

    // actual kronos call
    chronos_returncode status;
    ssize_t cret;
    int64_t ret = kronos_cl->weaver_order(wpair, num_pairs, &status, &cret);
    ret = kronos_cl->wait(ret, 100000, &status);

    // check kronos returned orders
    for (uint64_t i = 0; i < num_pairs; i++) {
        if (wpair[i].order != CHRONOS_HAPPENS_BEFORE) {
            delete[] wpair;
            return false;
        }
    }
    delete[] wpair;
    return true;
}
