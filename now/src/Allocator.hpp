#pragma once
#include <cstdlib>
#include <cassert>
#include <cstring> // for memset
#include <algorithm> // for std::find_if
#include "Logging.hpp"

namespace now
{
    struct Allocator;
    static Allocator* temp_allocator();
    static Allocator* heap_allocator();
    static Allocator* null_allocator();
    static Allocator* default_allocator();
    
    #ifdef NOW_DEBUG_MEMORY
    struct Allocation_Tracker
    {
        struct Allocation
        {
            void*  ptr  = nullptr;
            size_t size = 0;
        };

        const char* name = "default";
        std::vector<Allocation> allocations; // TODO: use a performant container when it will get too slow (unordered set, or something non std)

        Allocation_Tracker(const char* _name);

        void                after_allocate(void* ptr, size_t size);
        const Allocation*   find_allocation(void* ptr) const;
        void                after_reallocate(void* old_ptr, void* new_ptr, size_t new_size );
        void                register_before_release(void* ptr);
    };
    #endif

    struct Allocator
    {
        using AllocateFunctionType   = void* (size_t size);
        using ReleaseFunctionType    = void  (void*  ptr );
        using ReallocateFunctionType = void* (void*  ptr, size_t size);

        const char*             class_name;
        AllocateFunctionType*   allocate;
        ReleaseFunctionType*    release;
        ReallocateFunctionType* reallocate;

        ~Allocator()
        {
            class_name = "destroyed";
            allocate   = nullptr;
            release    = nullptr;
            reallocate = nullptr;
        }

        template<typename AllocatorType>
        static Allocator construct_from(const char* _name)
        {
            LOG_DEBUG("[mem] new %s\n", _name);
            return Allocator{
                _name,
                &AllocatorType::allocate,
                &AllocatorType::release,
                &AllocatorType::reallocate,
            };
        }
    };

    struct Heap_Allocator
    {
        static Allocation_Tracker& tracker();
        static void*    allocate(size_t size);
        static void     release(void* ptr);
        static void*    reallocate(void* ptr, size_t size);

    };
    
    struct Null_Allocator
    {
        static void*    allocate(size_t size);
        static void     release(void* ptr);
        static void*    reallocate(void* ptr, size_t size);
    };

    template<size_t SIZE_IN_BYTES>
    struct Ring_Buffer_Allocator
    {
        struct State
        {
            char   buffer[SIZE_IN_BYTES];
            char*  head;
            char*  prev_acquired;

            #ifdef NOW_DEBUG_MEMORY
                Allocation_Tracker tracker{"ring"};
            #endif

            constexpr State()
            : head(buffer) 
            , prev_acquired(nullptr)
            {
                #ifdef NOW_DEBUG_MEMORY
                    // Fill memory with R's in C-style string way
                    memset(buffer, 'R', SIZE_IN_BYTES);
                    buffer[SIZE_IN_BYTES-1] = 0;
                #endif
            }
        };

        static State& state()
        {
            static State s;
            return s;
        }

        static void* allocate(size_t size)
        {
            char* ptr = _acquire(size);

            #ifdef NOW_DEBUG_MEMORY
                state().tracker.after_allocate(ptr, size);
            #endif     

            return ptr;
        }

        static void release(void* ptr)
        {
            #ifdef NOW_DEBUG_MEMORY
                LOG_DEBUG("Ring_Buffer_Allocator::release() - nothing to do ...\n");
            #endif
        }

        static void* reallocate(void* ptr, size_t size)
        {
            // When ptr was previously aquired, we can simply extend it
            void* old_ptr = ptr;
            if ( old_ptr == state().prev_acquired )
            {
                state().head = reinterpret_cast<char*>(old_ptr);
            }
            
            void* new_ptr =  _acquire(size);

            #ifdef NOW_DEBUG_MEMORY
                state().tracker.after_reallocate(old_ptr, new_ptr, size);
            #endif

            return new_ptr;
        }

        static char* _acquire(size_t size)
        {
            assert( size < SIZE_IN_BYTES/2 && "Increase RingBuffer!");

            if ( size == 0 )
            {
                return nullptr;
            }

            if (state().head + size > state().buffer + SIZE_IN_BYTES)
            {
                state().head = 0;
                LOG_DEBUG("Ring buffer end reached, will overwrite from 0 from now.\n");
            }
            
            char* ptr = state().head;
            state().prev_acquired = ptr;
            state().head += size;
            return ptr;
        }
    };
}

#ifdef NOW_IMPLEMENTATION
#ifdef NOW_DEBUG_MEMORY

now::Allocation_Tracker& now::Heap_Allocator::tracker()
{
    static Allocation_Tracker heap_allocation_tracker{"heap"};
    return heap_allocation_tracker;
}

now::Allocation_Tracker::Allocation_Tracker(const char* _name)
: name(_name)
{
    allocations.reserve(512);
};

void now::Allocation_Tracker::after_allocate(void* ptr, size_t size)
{   
    assert(ptr != nullptr);
    assert( find_allocation(ptr) == nullptr );

    allocations.emplace_back(Allocation{ ptr, size });

    LOG_DEBUG("[mem:%s] malloc %lu byte(s) at %p\n", name, size, ptr);
}

const now::Allocation_Tracker::Allocation* now::Allocation_Tracker::find_allocation(void* ptr) const
{
    assert(ptr != nullptr);
    for(auto& each : allocations)
        if (each.ptr == ptr)
            return &each;
    return nullptr;
}

void now::Allocation_Tracker::after_reallocate(void* old_ptr, void* new_ptr, size_t new_size )
{
    assert(old_ptr != nullptr);
    assert(new_ptr != nullptr);

    auto old = std::find_if(
        allocations.begin(), allocations.end(),
        [old_ptr](auto& item){ return item.ptr == old_ptr; }
    );
    assert( old != allocations.end() && "Unknown address!");

    allocations.erase(old);
    allocations.push_back(Allocation{ new_ptr, new_size });

    LOG_DEBUG("[mem:%s] realloc %lu byte(s) at %p (previously: %p, %lu byte(s))\n", name, new_size, new_ptr, old->ptr, old->size);
}

void now::Allocation_Tracker::register_before_release(void* ptr)
{               
    assert(ptr != nullptr);        
    auto it = std::find_if(
        allocations.begin(), allocations.end(),
        [ptr](auto& item){ return item.ptr == ptr; }
    );

    if ( it == allocations.end() )
    {
        LOG_DEBUG("[mem:%s] WARN: allocation at %p will be released but: WAS NOT REGISTERED. Will crash.\n", name, ptr); 
        assert(false);
    }

    LOG_DEBUG("[mem:%s] released %p (known size: %lu)\n", name, it->ptr, it->size);

    if ( allocations.size() == 0 )
    {
        LOG_DEBUG("[mem:%s] all allocations have been released.\n", name);
    }
}
#endif // NOW_DEBUG_MEMORY

void* now::Heap_Allocator::allocate(size_t size)
{
    void* ptr = std::malloc(size);

    #ifdef NOW_DEBUG_MEMORY
        assert(ptr != nullptr);
        tracker().after_allocate(ptr, size);
    #endif
    
    return ptr;
}

void now::Heap_Allocator::release(void* ptr)
{
    #ifdef NOW_DEBUG_MEMORY
        tracker().register_before_release(ptr);
    #endif

    std::free(ptr);
}

void* now::Heap_Allocator::reallocate(void* ptr, size_t size)
{
    void* old_ptr = ptr;
    
    #ifdef NOW_DEBUG_MEMORY 
        assert(old_ptr != nullptr);
    #endif

    char* new_ptr = reinterpret_cast<char*>(std::realloc(old_ptr, size));

    #ifdef NOW_DEBUG_MEMORY                
        assert(new_ptr != nullptr);
        tracker().after_reallocate(old_ptr, new_ptr, size);
    #endif

    return new_ptr;
}

void* now::Null_Allocator::allocate(size_t size)
{
    assert(false && "NullAllocator cannot allocate!");
    return nullptr;
}

void now::Null_Allocator::release(void* ptr)
{
    assert(false && "NullAllocator cannot release!");
}

void* now::Null_Allocator::reallocate(void* ptr, size_t size)
{
    assert(false && "NullAllocator cannot reallocate!");
    return nullptr;
}

now::Allocator* now::temp_allocator()
{
    static Allocator temp_allocator = Allocator::construct_from<Ring_Buffer_Allocator<5*1024*1024>>("temp");
    return &temp_allocator;
}

now::Allocator* now::heap_allocator()
{
    static Allocator heap_allocator = Allocator::construct_from<Heap_Allocator>("heap");
    return &heap_allocator;
}

static now::Allocator* now::null_allocator()
{
    static Allocator null_allocator = Allocator::construct_from<Null_Allocator>("null");
    return &null_allocator;
}

now::Allocator* now::default_allocator()
{
    return heap_allocator();
}

#endif // NOW_IMPLEMENTATION
