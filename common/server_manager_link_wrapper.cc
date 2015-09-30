/*
 * ===============================================================
 *    Description:  Coordinator link wrapper implementation.
 *
 *        Created:  2014-02-10 14:57:37
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

#define __STDC_LIMIT_MACROS

// C
#include <stdint.h>

// POSIX
#include <signal.h>

// e
#include <e/endian.h>
#include <e/serialization.h>

// Weaver
#define weaver_debug_
#include "common/weaver_constants.h"
#include "common/server_manager_returncode.h"
#include "common/server_manager_link_wrapper.h"
#include "common/passert.h"

using po6::threads::make_thread_wrapper;

class server_manager_link_wrapper::sm_rpc
{
    public:
        sm_rpc();
        virtual ~sm_rpc() throw ();

    public:
        virtual bool callback(server_manager_link_wrapper* clw);

    public:
        replicant_returncode status;
        char* output;
        size_t output_sz;
        std::ostringstream msg;

    private:
        sm_rpc(const sm_rpc&);
        sm_rpc& operator = (const sm_rpc&);

    private:
        void inc() { ++m_ref; }
        void dec() { if (--m_ref == 0) delete this; }
        friend class e::intrusive_ptr<sm_rpc>;

    private:
        size_t m_ref;
};

server_manager_link_wrapper :: sm_rpc :: sm_rpc()
    : status(REPLICANT_GARBAGE)
    , output(NULL)
    , output_sz(0)
    , msg()
    , m_ref(0)
{
}

server_manager_link_wrapper :: sm_rpc :: ~sm_rpc() throw ()
{
    if (output)
    {
        free(output);
    }
}

bool
server_manager_link_wrapper :: sm_rpc :: callback(server_manager_link_wrapper* clw)
{
    if (status != REPLICANT_SUCCESS)
    {
        WDEBUG << "server manager error: returncode=" << replicant_returncode_to_string(status)
               << ", msg=" << msg.str()
               << ": " << clw->m_sm->error_message() << " @ " << clw->m_sm->error_location() << std::endl;
    }

    if (status == REPLICANT_CLUSTER_JUMP)
    {
        clw->do_sleep();
    }

    return false;
}

server_manager_link_wrapper :: server_manager_link_wrapper(server_id us, std::shared_ptr<po6::net::location> loc)
    : m_us(us)
    , m_loc(loc)
    , m_poller(make_thread_wrapper(&server_manager_link_wrapper::background_maintenance, this))
    , m_deferred()
    , m_sm()
    , m_rpcs()
    , m_mtx()
    , m_cond(&m_mtx)
    , m_poller_started(false)
    , m_locked(false)
    , m_kill(false)
    , m_to_kill()
    , m_waiting(0)
    , m_sleep(1000ULL * 1000ULL)
    , m_online_id(-1)
    , m_shutdown_requested(false)
    , m_need_config_ack(false)
    , m_config_ack(0)
    , m_config_ack_id(-1)
    , m_need_config_stable(false)
    , m_config_stable(0)
    , m_config_stable_id(-1)
{
}

server_manager_link_wrapper :: ~server_manager_link_wrapper() throw ()
{
    if (m_poller_started)
    {
        m_poller.join();
    }
}

void
server_manager_link_wrapper :: set_server_manager_address(const char* host, uint16_t port)
{
    PASSERT(!m_sm.get());
    m_sm.reset(new server_manager_link(host, port));
}

bool
server_manager_link_wrapper :: get_unique_number(uint64_t &id)
{
    return m_sm->get_unique_number(id);
}

bool
server_manager_link_wrapper :: register_id(server_id us, const po6::net::location& bind_to, server::type_t type)
{
    m_us = us;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    std::auto_ptr<e::buffer> buf(e::buffer::create(sizeof(uint64_t) + e::pack_size(bind_to) + sizeof(int)));
#pragma GCC diagnostic pop
    e::packer pa = buf->pack_at(0);
    uint8_t t = static_cast<uint8_t>(type);
    pa = pa << us << bind_to << t;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    std::auto_ptr<sm_rpc> rpc(new sm_rpc);
#pragma GCC diagnostic pop
    int64_t rid = m_sm->rpc("server_register",
                            reinterpret_cast<const char*>(buf->data()), buf->size(),
                            &rpc->status,
                            &rpc->output,
                            &rpc->output_sz);

    if (rid < 0)
    {
        WDEBUG << "could not register as " << us
               << ": " << m_sm->error_message()
               << " @ " << m_sm->error_location() << std::endl;
        return false;
    }

    replicant_returncode lrc = REPLICANT_GARBAGE;
    int64_t lid = m_sm->wait(rid, -1, &lrc);

    if (lid < 0)
    {
        WDEBUG << "could not register as " << us
               << ": " << m_sm->error_message()
               << " @ " << m_sm->error_location() << std::endl;
        return false;
    }

    if (lid != rid)
    {
        WDEBUG << "could not register as " << us << ": server manager loop malfunction" << std::endl;
        WDEBUG << "lid=" << lid << ", rid=" << rid << std::endl;
        return false;
    }

    if (rpc->status != REPLICANT_SUCCESS)
    {
        WDEBUG << "could not register as " << us
               << ": " << m_sm->error_message()
               << " @ " << m_sm->error_location() << std::endl;
        return false;
    }

    if (rpc->output_sz >= 2)
    {
        uint16_t x;
        e::unpack16be(rpc->output, &x);
        server_manager_returncode rc = static_cast<server_manager_returncode>(x);

        switch (rc)
        {
            case COORD_SUCCESS:
                break;
            case COORD_DUPLICATE:
                WDEBUG << "could not register as " << us << ": another server has this ID" << std::endl;
                return false;
            case COORD_UNINITIALIZED:
                WDEBUG << "could not register as " << us << ": server manager not initialized" << std::endl;
                return false;
            case COORD_MALFORMED:
            case COORD_NOT_FOUND:
            case COORD_NO_CAN_DO:
            default:
                WDEBUG << "could not register as " << us << ": server manager returned " << rc << std::endl;
                return false;
        }
    }
    else
    {
        WDEBUG << "could not register as " << us << ": server manager returned invalid message" << std::endl;
        return false;
    }

    while (!m_sm->config()->exists(us))
    {
        lid = m_sm->wait(INT64_MAX, -1, &lrc);

        if (lid < 0)
        {
            LOG(ERROR) << "could not register as " << us << ": coordinator loop malfunction";
            return false;
        }
    }

    return true;
}

bool
server_manager_link_wrapper :: should_exit()
{
    enter_critical_section();
    bool yes =  (!m_sm->config()->exists(m_us) && m_sm->config()->version() > 0) ||
                (m_shutdown_requested && m_sm->config()->get_state(m_us) == server::SHUTDOWN);
    exit_critical_section();

    return yes;
}

bool
server_manager_link_wrapper :: maintain_link()
{
    enter_critical_section_killable();
    bool exit_status = false;

    if (!m_poller_started)
    {
        m_poller.start();
        m_poller_started = true;
    }

    while (true)
    {
        ensure_available();
        ensure_config_ack();
        ensure_config_stable();
        replicant_returncode status = REPLICANT_GARBAGE;
        int64_t id = -1;

        if (!m_deferred.empty())
        {
            id = m_deferred.front().first;
            status = m_deferred.front().second;
            m_deferred.pop();
        }
        else
        {
            id = m_sm->loop(1000, &status);
        }

        if (id < 0 &&
            (status == REPLICANT_TIMEOUT ||
             status == REPLICANT_INTERRUPTED))
        {
            reset_sleep();
            exit_status = false;
            break;
        }
        else if (id < 0 && (status == REPLICANT_COMM_FAILED))
        {
            WDEBUG << "server manager disconnected: backing off before retrying" << std::endl;
            WDEBUG << "details: " << m_sm->error_message() << " @ " << m_sm->error_location() << std::endl;
            do_sleep();
            exit_status = false;
            break;
        }
        else if (id < 0 && status == REPLICANT_CLUSTER_JUMP)
        {
            WDEBUG << "cluster jump: " << m_sm->error_message() << " @ " << m_sm->error_location() << std::endl;
            do_sleep();
            exit_status = false;
            break;
        }
        else if (id < 0)
        {
            WDEBUG << "server manager error: " << m_sm->error_message() << " @ " << m_sm->error_location() << std::endl;
            do_sleep();
            exit_status = false;
            break;
        }

        reset_sleep();

        if (id == INT64_MAX)
        {
            exit_status = m_sm->config()->exists(m_us);
            break;
        }

        rpc_map_t::iterator it = m_rpcs.find(id);

        if (it == m_rpcs.end())
        {
            continue;
        }

        e::intrusive_ptr<sm_rpc> rpc = it->second;
        m_rpcs.erase(it);

        if (rpc->callback(this))
        {
            break;
        }
    }

    exit_critical_section_killable();
    return exit_status;
}

const configuration&
server_manager_link_wrapper :: config()
{
    return *m_sm->config();
}

void
server_manager_link_wrapper :: request_shutdown()
{
    m_shutdown_requested = true;
    char buf[sizeof(uint64_t)];
    e::pack64be(m_us.get(), buf);
    e::intrusive_ptr<sm_rpc> rpc = new sm_rpc();
    rpc->msg << "request shutdown";
    make_rpc("server_shutdown", buf, sizeof(uint64_t), rpc);
}

void
server_manager_link_wrapper :: config_ack(uint64_t version)
{
    enter_critical_section();

    if (m_config_ack < version)
    {
        m_need_config_ack = true;
        m_config_ack = version;
        m_config_ack_id = -1;
        ensure_config_ack();
    }

    exit_critical_section();
}

void
server_manager_link_wrapper :: config_stable(uint64_t version)
{
    enter_critical_section();

    if (m_config_stable < version)
    {
        m_need_config_stable = true;
        m_config_stable = version;
        m_config_stable_id = -1;
        ensure_config_stable();
    }

    exit_critical_section();
}

void
server_manager_link_wrapper :: report_tcp_disconnect(uint64_t id)
{
    char buf[2 * sizeof(uint64_t)];
    e::pack64be(id, buf);
    uint64_t version = m_sm->config()->version();
    e::pack64be(version, buf);
    e::intrusive_ptr<sm_rpc> rpc = new sm_rpc();
    rpc->msg << "report TCP disconnect id=" << id;
    make_rpc("report_disconnect", buf, 2*sizeof(uint64_t), rpc);
}

void
server_manager_link_wrapper :: background_maintenance()
{
    sigset_t ss;

    if (sigfillset(&ss) < 0)
    {
        PLOG(ERROR) << "sigfillset";
        return;
    }

    if (pthread_sigmask(SIG_BLOCK, &ss, NULL) < 0)
    {
        PLOG(ERROR) << "could not block signals";
        return;
    }

    while (true)
    {
        enter_critical_section_background();

        replicant_returncode status = REPLICANT_GARBAGE;
        int64_t id = m_sm->loop(1000, &status);

        if (status != REPLICANT_TIMEOUT && status != REPLICANT_INTERRUPTED)
        {
            m_deferred.push(std::make_pair(id, status));
        }

        exit_critical_section_killable();
    }
}

void
server_manager_link_wrapper :: do_sleep()
{
    uint64_t sleep = m_sleep;
    timespec ts;

    while (sleep > 0)
    {
        ts.tv_sec = 0;
        ts.tv_nsec = std::min(static_cast<uint64_t>(1000ULL * 1000ULL), sleep);
        sigset_t empty_signals;
        sigset_t old_signals;
        sigemptyset(&empty_signals); // should never fail
        pthread_sigmask(SIG_SETMASK, &empty_signals, &old_signals); // should never fail
        nanosleep(&ts, nullptr); // nothing to gain by checking output
        pthread_sigmask(SIG_SETMASK, &old_signals, nullptr); // should never fail
        sleep -= ts.tv_nsec;
    }

    m_sleep = std::min(static_cast<uint64_t>(1000ULL * 1000ULL * 1000ULL), m_sleep * 2);
}

void
server_manager_link_wrapper :: reset_sleep()
{
    uint64_t start_sleep = 1000ULL * 1000ULL;

    if (m_sleep != start_sleep)
    {
        m_sleep = start_sleep;
        WDEBUG << "connection to server manager reestablished" << std::endl;
    }
}

void
server_manager_link_wrapper :: enter_critical_section()
{
    po6::threads::mutex::hold hold(&m_mtx);

    while (m_locked)
    {
        if (m_kill)
        {
            pthread_kill(m_to_kill, SIGUSR1);
        }

        ++m_waiting;
        m_cond.wait();
        --m_waiting;
    }

    m_locked = true;
    m_kill = false;
}

void
server_manager_link_wrapper :: exit_critical_section()
{
    po6::threads::mutex::hold hold(&m_mtx);
    m_locked = false;
    m_kill = false;

    if (m_waiting > 0)
    {
        m_cond.broadcast();
    }
}

void
server_manager_link_wrapper :: enter_critical_section_killable()
{
    po6::threads::mutex::hold hold(&m_mtx);

    while (m_locked)
    {
        if (m_kill)
        {
            pthread_kill(m_to_kill, SIGUSR1);
        }

        ++m_waiting;
        m_cond.wait();
        --m_waiting;
    }

    m_locked = true;
    m_kill = true;
    m_to_kill = pthread_self();
}

void
server_manager_link_wrapper :: enter_critical_section_background()
{
    po6::threads::mutex::hold hold(&m_mtx);

    while (m_locked || m_waiting > 0)
    {
        ++m_waiting;
        m_cond.wait();
        --m_waiting;
    }

    m_locked = true;
    m_kill = true;
    m_to_kill = pthread_self();
}

void
server_manager_link_wrapper :: exit_critical_section_killable()
{
    po6::threads::mutex::hold hold(&m_mtx);
    m_locked = false;
    m_kill = false;

    if (m_waiting > 0)
    {
        m_cond.broadcast();
    }
}

class server_manager_link_wrapper::sm_rpc_available : public sm_rpc
{
    public:
        sm_rpc_available() {}
        virtual ~sm_rpc_available() throw () {}

    public:
        virtual bool callback(server_manager_link_wrapper* clw);
};

bool
server_manager_link_wrapper :: sm_rpc_available :: callback(server_manager_link_wrapper* clw)
{
    sm_rpc::callback(clw);
    clw->m_online_id = -1;
    return false;
}

void
server_manager_link_wrapper :: ensure_available()
{
    if (m_online_id >= 0 || m_shutdown_requested)
    {
        return;
    }

    if (m_sm->config()->get_address(m_us) == *m_loc &&
        m_sm->config()->get_state(m_us) == server::AVAILABLE)
    {
        return;
    }

    size_t sz = sizeof(uint64_t) + e::pack_size(*m_loc);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    std::auto_ptr<e::buffer> enter_buf(e::buffer::create(sz));
#pragma GCC diagnostic pop
    enter_buf->pack() << m_us << *m_loc;
    char exit_buf[sizeof(uint64_t)];
    e::pack64be(m_us.get(), exit_buf);
    e::intrusive_ptr<sm_rpc> rpc = new sm_rpc_available();
    rpc->msg << "server online";
    m_online_id = make_rpc_defended("server_online",
                                    enter_buf->cdata(), enter_buf->size(),
                                    "server_suspect", exit_buf, sizeof(uint64_t),
                                    rpc);
}

class server_manager_link_wrapper::sm_rpc_config_ack : public sm_rpc
{
    public:
        sm_rpc_config_ack() {}
        virtual ~sm_rpc_config_ack() throw () {}

    public:
        virtual bool callback(server_manager_link_wrapper* clw);
};

bool
server_manager_link_wrapper :: sm_rpc_config_ack :: callback(server_manager_link_wrapper* clw)
{
    sm_rpc::callback(clw);
    clw->m_config_ack_id = -1;
    clw->m_need_config_ack = status != REPLICANT_SUCCESS;
    return false;
}

void
server_manager_link_wrapper :: ensure_config_ack()
{
    if (m_config_ack_id >= 0 || !m_need_config_ack)
    {
        return;
    }

    char buf[2 * sizeof(uint64_t)];
    e::pack64be(m_us.get(), buf);
    e::pack64be(m_config_ack, buf + sizeof(uint64_t));
    e::intrusive_ptr<sm_rpc> rpc = new sm_rpc_config_ack();
    rpc->msg << "ack config version=" << m_config_ack;
    m_config_ack_id = make_rpc_nosync("config_ack", buf, 2 * sizeof(uint64_t), rpc);
}

class server_manager_link_wrapper::sm_rpc_config_stable : public sm_rpc
{
    public:
        sm_rpc_config_stable() {}
        virtual ~sm_rpc_config_stable() throw () {}

    public:
        virtual bool callback(server_manager_link_wrapper* clw);
};

bool
server_manager_link_wrapper :: sm_rpc_config_stable :: callback(server_manager_link_wrapper* clw)
{
    sm_rpc::callback(clw);
    clw->m_config_stable_id = -1;
    clw->m_need_config_stable = status != REPLICANT_SUCCESS;
    return false;
}

void
server_manager_link_wrapper :: ensure_config_stable()
{
    if (m_config_stable_id >= 0 || !m_need_config_stable)
    {
        return;
    }

    char buf[2 * sizeof(uint64_t)];
    e::pack64be(m_us.get(), buf);
    e::pack64be(m_config_stable, buf + sizeof(uint64_t));
    e::intrusive_ptr<sm_rpc> rpc = new sm_rpc_config_stable();
    rpc->msg << "stable config version=" << m_config_stable;
    m_config_stable_id = make_rpc_nosync("config_stable", buf, 2 * sizeof(uint64_t), rpc);
}

void
server_manager_link_wrapper :: make_rpc(const char* func,
                                     const char* data, size_t data_sz,
                                     e::intrusive_ptr<sm_rpc> rpc)
{
    enter_critical_section();
    make_rpc_nosync(func, data, data_sz, rpc);
    exit_critical_section();
}

int64_t
server_manager_link_wrapper :: make_rpc_nosync(const char* func,
                                            const char* data, size_t data_sz,
                                            e::intrusive_ptr<sm_rpc> rpc)
{
    int64_t id = m_sm->rpc(func, data, data_sz,
                              &rpc->status,
                              &rpc->output,
                              &rpc->output_sz);

    if (id < 0)
    {
        WDEBUG << "server manager error: " << rpc->msg.str()
                   << ": " << m_sm->error_message()
                   << " @ " << m_sm->error_location() << std::endl;
    }
    else
    {
        m_rpcs.insert(std::make_pair(id, rpc));
    }

    return id;
}

int64_t
server_manager_link_wrapper :: make_rpc_defended(const char* enter_func,
                                                 const char* enter_data, size_t enter_data_sz,
                                                 const char* exit_func,
                                                 const char* exit_data, size_t exit_data_sz,
                                                 e::intrusive_ptr<sm_rpc> rpc)
{
    int64_t id = m_sm->rpc_defended(enter_func, enter_data, enter_data_sz,
                                    exit_func, exit_data, exit_data_sz,
                                    &rpc->status);

    if (id < 0)
    {
        LOG(ERROR) << "coordinator error: " << rpc->msg.str()
                   << ": " << m_sm->error_message()
                   << " @ " << m_sm->error_location();
    }
    else
    {
        m_rpcs.insert(std::make_pair(id, rpc));
    }

    return id;
}

int64_t
server_manager_link_wrapper :: wait_nosync(const char* cond, uint64_t state,
                                        e::intrusive_ptr<sm_rpc> rpc)
{
    int64_t id = m_sm->wait(cond, state, &rpc->status);

    if (id < 0)
    {
        WDEBUG << "server manager error: " << rpc->msg.str()
               << ": " << m_sm->error_message()
               << " @ " << m_sm->error_location() << std::endl;
    }
    else
    {
        m_rpcs.insert(std::make_pair(id, rpc));
    }

    return id;
}

#undef weaver_debug_
