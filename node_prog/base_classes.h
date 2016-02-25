#ifndef weaver_node_prog_base_classes_h_
#define weaver_node_prog_base_classes_h_

#include <e/buffer.h>
#include <vector>
#include <unordered_set>
#include <iostream>

#include "common/types.h"

namespace node_prog
{
    class Packable 
    {
        public:
            virtual uint64_t size(void*) const  = 0;
            virtual void pack(e::packer& packer, void*) const = 0;
            virtual void unpack(e::unpacker& unpacker, void*) = 0;
    };

    class Deletable 
    {
        public:
            virtual ~Deletable() = 0;
    };

    inline Deletable::~Deletable() 
    { 
        /* destructor must be defined */ 
    }

    class Node_Parameters_Base : public virtual Packable
    {
        virtual bool search_cache() = 0;
        virtual cache_key_t cache_key() = 0;
    };

    class Node_State_Base : public virtual Packable, public virtual Deletable 
    {
        public:
            virtual ~Node_State_Base() { } // << important
            std::unordered_set<uint64_t> contexts_found;
    };

    class Cache_Value_Base : public virtual Packable, public virtual Deletable
    {
    };

    typedef std::shared_ptr<Node_Parameters_Base> np_param_ptr_t;
    typedef std::shared_ptr<Node_State_Base> np_state_ptr_t;
}

#endif
