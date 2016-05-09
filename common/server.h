/*
 * ===============================================================
 *    Description:  Server state for cluster configuration.
 *
 *        Created:  2014-02-08 17:26:46
 *
 *         Author:  Robert Escriva, escriva@cs.cornell.edu
 *                  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

// Most of the following code has been 'borrowed' from
// Robert Escriva's HyperDex coordinator.
// see https://github.com/rescrv/HyperDex for the original code.

#ifndef weaver_common_server_h_
#define weaver_common_server_h_

// po6
#include <po6/net/location.h>

// e
#include <e/buffer.h>

// Weaver
#include "common/ids.h"

class server
{
    public:
        enum state_t
        {
            ASSIGNED = 1,
            NOT_AVAILABLE = 2,
            AVAILABLE = 3,
            SHUTDOWN = 4,
            KILLED = 5
        };
        enum type_t
        {
            UNDEF = 0,
            SHARD = 1,
            VT = 2,
            BACKUP_SHARD = 3,
            BACKUP_VT = 4
        };
        static const char* to_string(state_t state);
        static const char* to_string(type_t type);

    public:
        server();
        explicit server(const server_id&);
        bool operator==(const server &other) const;
        bool operator!=(const server &other) const;

    public:
        state_t state;
        server_id id; // random int
        // increasing counter signifying order in which this server was registered
        uint64_t weaver_id;
        // if virtual id of a server is v, it is the v^{th} vt or shard that was registered
        // to send a message to a vt with virtual id v, you send(v, ...)
        // to send a message to a shard with virtual id v, you send to (v+NumVts, ...)
        uint64_t virtual_id;
        type_t type;
        po6::net::location bind_to;
};

bool
operator < (const server& lhs, const server& rhs);

e::packer
operator << (e::packer lhs, const server& rhs);
e::unpacker
operator >> (e::unpacker lhs, server& rhs);
size_t
pack_size(const server& p);

#endif
