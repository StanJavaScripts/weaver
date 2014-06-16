/*
 * ===============================================================
 *    Description:  DS to keep track of transaction replies from
 *                  shards.
 *
 *        Created:  2014-02-24 12:20:10
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013-2014, Cornell University, see the LICENSE
 *                     file for licensing agreement
 * ===============================================================
 */

#ifndef weaver_coordinator_current_tx_h_
#define weaver_coordinator_current_tx_h_

#include "common/transaction.h"

namespace coordinator
{
    struct current_tx
    {
        uint64_t count;
        std::bitset<NUM_VTS> locks;
        std::unique_ptr<transaction::pending_tx> tx;

        current_tx() : count(0) { }

        current_tx(std::unique_ptr<transaction::pending_tx> t)
            : count(0)
            , tx(std::move(t))
        { }
    };
}

#endif
