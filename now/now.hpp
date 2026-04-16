#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <cstdlib>
#include <filesystem> // for filesystem::exists
#include <iostream>
#include <algorithm>

//-----------------------------------------------------------------------------
// MACROS
// ----------------------------------------------------------------------------

#define LOG(FMT, ...) now::log_message(FMT, __VA_ARGS__ )

#ifdef NOW_VERBOSE
#   define LOG_DEBUG(FMT, ...) now::log_message("[debug] " FMT, __VA_ARGS__ )
#else
#   define LOG_DEBUG(FMT, ...)
#endif

#define TASK( TASK_NAME, ...) \
    static const char* TASK_NAME = #TASK_NAME; \
    now::new_task( \
        now::get_state(), \
        now::Task::Type_REGULAR,\
        #TASK_NAME, \
        {__VA_ARGS__} \
    )->action = [](const now::Task* task) -> void

#define FILETASK( FILE_NAME, ...) \
    now::new_task( \
        now::get_state(), \
        now::Task::Type_FILE,\
        FILE_NAME, \
        {__VA_ARGS__} \
    )->action = [](const now::Task* task) -> void

//-----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

namespace now
{
    struct Allocator;
    static Allocator* temp_allocator();
    static Allocator* heap_allocator();
    // static Allocator* null_allocator();
    static Allocator* default_allocator();

    void log_message(const char *format, ...);

    //-----------------------------------------------------------------------------
    // ALLOCATORS
    // ----------------------------------------------------------------------------

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

        Allocation_Tracker(const char* _name)
        : name(_name)
        {
            allocations.reserve(512);
        };

        void after_allocate(void* ptr, size_t size)
        {   
            assert(ptr != nullptr);
            assert( find_allocation(ptr) == nullptr );

            allocations.emplace_back(Allocation{ ptr, size });

            LOG_DEBUG("[mem:%s] malloc %lu byte(s) at %p\n", name, size, ptr);
        }

        const Allocation* find_allocation(void* ptr) const
        {
            assert(ptr != nullptr);
            for(auto& each : allocations)
                if (each.ptr == ptr)
                    return &each;
            return nullptr;
        }

        void after_reallocate(void* old_ptr, void* new_ptr, size_t new_size )
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

        void register_before_release(void* ptr)
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
    };
#endif

    struct Heap_Allocator
    {
        #ifdef NOW_DEBUG_MEMORY
            static Allocation_Tracker& tracker()
            {
                static Allocation_Tracker heap_allocation_tracker{"heap"};
                return heap_allocation_tracker;
            }
        #endif

        static void* allocate(size_t size)
        {
            void* ptr = malloc(size);

            #ifdef NOW_DEBUG_MEMORY
                assert(ptr != nullptr);
                tracker().after_allocate(ptr, size);
            #endif
            
            return ptr;
        }

        static void release(void* ptr)
        {
            #ifdef NOW_DEBUG_MEMORY
                tracker().register_before_release(ptr);
            #endif

            free(ptr);
        }

        static void* reallocate(void* ptr, size_t size)
        {
            void* old_ptr = ptr;
            
            #ifdef NOW_DEBUG_MEMORY 
                assert(old_ptr != nullptr);
            #endif

            char* new_ptr = reinterpret_cast<char*>(realloc(old_ptr, size));

            #ifdef NOW_DEBUG_MEMORY                
                assert(new_ptr != nullptr);
                tracker().after_reallocate(old_ptr, new_ptr, size);
            #endif

            return new_ptr;
        }

    };
    
    struct Null_Allocator
    {
        static void* allocate(size_t size)
        { assert(false && "NullAllocator cannot allocate!"); return nullptr; }

        static void release(void* ptr)
        { assert(false && "NullAllocator cannot release!"); }

        static void* reallocate(void* ptr, size_t size)
        { assert(false && "NullAllocator cannot reallocate!"); return nullptr; }

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

    static Allocator* temp_allocator()
    {
        static Allocator temp_allocator = Allocator::construct_from<Ring_Buffer_Allocator<5*1024*1024>>("temp");
        return &temp_allocator;
    }

    static Allocator* heap_allocator()
    {
        static Allocator heap_allocator = Allocator::construct_from<Heap_Allocator>("heap");
        return &heap_allocator;
    }

    // static Allocator* null_allocator()
    // {
    //     static Allocator null_allocator = Allocator::construct_from<Null_Allocator>("null");
    //     return &null_allocator;
    // }

    static Allocator* default_allocator()
    { return heap_allocator(); }

    //-----------------------------------------------------------------------------
    // STRINGS
    // ----------------------------------------------------------------------------

    struct String
    {
        static constexpr size_t npos = (size_t)-1;

        size_t size = {0};
        char*  data = {nullptr};
        Allocator* allocator = nullptr;

        String()
        {
            allocator = heap_allocator();
        }

        String(Allocator* _allocator)
        : allocator(_allocator)
        {
            assert(_allocator != nullptr);
        }

        String(const char* str)
        : size(strlen(str))
        , data(const_cast<char*>(str))
        , allocator(nullptr)
        {}

        String(size_t _size, char* _data)
        : size(_size)
        , data(_data)
        {
            allocator = heap_allocator();
        }

// #define NOW_STD_COMPATIBILITY
// #ifdef NOW_STD_COMPATIBILITY
//         explicit String(const std::string& str)
//         : size(str.size())
//         , data(const_cast<char*>(str.data()))
//         {}
// #endif

        ~String()
        {

        }

        void init(size_t _size)
        {
            assert(data == nullptr);
            assert(allocator != nullptr);
            size = _size;
            size_t safe_size = _size + 1; // for '\0'
            data = reinterpret_cast<char*>( allocator->allocate( safe_size ) );
            data[safe_size-1] = 0;
        }

        void release()
        {
            assert( allocator != nullptr );
            allocator->release(data);
            data = nullptr;
        }

        char operator[](size_t pos)
        { assert(pos < size && "out of bounds"); return data[pos]; }

        size_t rfind(char c)
        {
            size_t cursor = size-1;
            while ( cursor != npos && data[cursor] != c)
            {
                --cursor;
            }

            return cursor;
        }

        String lsplit(size_t index)
        {
            assert(index < size && "Out of bounds");
            return String{ index, data };
        }

        String rsplit(size_t index)
        {
            assert(index < size && "Out of bounds");
            return String{ size - index, data + index };
        }

        String basename()
        {
            size_t last_slash = rfind('\\');
            if ( last_slash == npos )
                return *this;
            return rsplit(last_slash+1);
        }

        String stem()
        {
            size_t index = rfind('.');
            if( index == npos )
                return *this;
            return lsplit(index);
        }

        char* cstr() const // TODO: RingBuffer should be generic (usr virtuals or delegates)
        {
            return data;
        }

        static String copy(size_t source_size, char* source_data, Allocator* copy_allocator = default_allocator() )
        { return String::copy(String{source_size, source_data}, copy_allocator); }

        static String copy(const String source, Allocator* copy_allocator = default_allocator())
        {
            String result{copy_allocator};
            result.init(source.size);
            std::memcpy(result.data, source.data, source.size);
            assert(source.size == result.size);
            assert(source.data != result.data);
            return result;
        }

        static int equals(const String& a, const String& b)
        {
            return a.size == b.size && strncmp(a.data, b.data, a.size) == 0;
        }
    };
            
    template<typename T>
    struct Array
    {
        static constexpr size_t invalid_index = (size_t)-1;

        size_t      size      = 0;
        void*       data      = nullptr;
        Allocator*  allocator = nullptr;
        //size_t      capacity    = 0;

        Array()
        : size(0)
        , data(nullptr)
        //, capacity(0)
        {
            allocator = default_allocator();
        }

        Array(Allocator* _allocator)
        : size(0)
        , data(nullptr)
        , allocator(_allocator)
        //, capacity(0)
        {
            assert(_allocator != nullptr);
        }

        Array(std::initializer_list<T> list)
        : size(0)
        , data(nullptr)
        //, capacity(0)
        {
            allocator = default_allocator();
            this->resize(list.size());
            for(size_t i = 0; i < size; i++)
                at(i) = *(list.begin()+i);
        }

        void release()
        {
            assert(allocator != nullptr);
            
            // for( size_t i = 0; i < size; i++)
            //     at(i).~T();

            allocator->release(data);
            data     = nullptr;
            size     = 0;
            //capacity = 0;
        }

        void resize(size_t new_size)
        {
            assert(new_size >= size);
            assert(allocator != nullptr);

            // ensure has capacity
            if( new_size > size )
            {
                if( data == nullptr )
                {
                    assert(allocator != nullptr);
                    data = reinterpret_cast<T*>(allocator->allocate( new_size * sizeof(T)));
                    assert(data!=nullptr);
                    size = new_size;
                    
                    for( size_t i=0; i < new_size; i++)
                        new (&at(i)) T(); // construct in-place
                }
                else
                {
                    void*  old_data = data;
                    size_t old_size = size;

                    assert(allocator != nullptr);
                    data = reinterpret_cast<T*>(allocator->reallocate(old_data, new_size * sizeof(T)));
                    assert(data!=nullptr);
                    size = new_size;

                    std::memcpy((void*)data, old_data, old_size);
                    
                    for( size_t i=old_size; i < new_size; i++)
                        new (&at(i)) T(); // construct in-place
                }
            }
            
        }

        void append(const T& str)
        {
            size_t index = size;
            resize( index + 1 );
            *(((T*)data) + index) = str;
        }

        void concat(const Array<T>& other)
        {
            size_t old_size = size;
            resize(size + other.size);
            for (size_t i = 0; i < other.size; ++i )
            {
                this->at(old_size+i) = other.at(i);
            }
        }

        const T& at(size_t pos) const
        { assert(pos < size && "out of bounds"); return *(((T*)data) + pos); }

        T& at(size_t pos)
        { assert(pos < size && "out of bounds"); return *(((T*)data) + pos); }
    };

    struct StringBuilder
    {
        Array<String> data;

        StringBuilder()
        : data()
        {
            data.allocator = default_allocator();
        }

        StringBuilder(Allocator* allocator)
        : data()
        {
            data.allocator = allocator;
        }

        StringBuilder&  append(const char* str);
        StringBuilder&  append(String str);
        template<typename T>  
        StringBuilder&  append(const Array<T>& arr);              
        String          build_string(String separator = "", Allocator* allocator = default_allocator() );
    };

    int     system(const String& command, const Array<String>& args= {}, bool fatal = true);
    void    remove(const String& path);
    void    rename(const String& src, const String& dst);
    int     mkdir_p(const String& path);
    bool    exists(const String& path);

    String  join(const Array<String>& arr, String separator, Allocator* string_allocator = default_allocator() );
    String  join(const Array<String>& arr, Allocator* string_allocator = default_allocator() )
    { return join(arr, "", string_allocator ); }

    typedef int Code;
    enum Code_ {
        Code_FAILED      = 0,
        Code_OK,
        Code_OK_SKIPPED
    };

    struct Task
    {
        typedef int Type;
        enum Type_ {
            Type_NULL = 0,
            Type_REGULAR = 1,
            Type_FILE = 2
        };

        using Action = void(*)(const Task*);
        static void null_action(const Task*) {}

        mutable bool        done = false;
        Type                type = Type_NULL;
        String              name = "";
        String              desc = "";
        Array<String>       deps;
        Action              action = &null_action;
    };

    struct State
    {
        struct StringHash
        {
            using is_transparent = void;
            size_t operator()(String s) const   { return std::hash<std::string_view>{}( s.cstr() ); }
        };

        struct StringEqual
        {
            using is_transparent = void;
            bool operator()(String a, String b) const { return String::equals(a,b); }
        };

        String binary;
        std::unordered_map<String, Task, StringHash, StringEqual> tasks;
    };

    static State&       get_state();
    static Task*        new_task( State& state, Task::Type type, String name, Array<String> deps);
    static const Task*  find_task(const State& state, const String& name);
    static Code         invoke_task(const State& state, const Task* task);
    static void         reset_state(State& state);
    static void         print_tasks(State& state);
    static void         print_help(State& state);
    static Code         task_invoke_sequence(const State& state, const std::vector<Task*>& tasks);
    static int          parse_args(int argc, char* argv[]);
    static void         compile_object(now::String src);
    static void         link(String binary, Array<String>& objects);
    static void         init();
    static void         rebuild_it_self_if_needed(String binary, String source);
    #ifdef NOW_ENABLE_TESTS
    static void         run_tests();
    #endif
}

#ifdef NOW_IMPLEMENTATION
now::String now::join(const Array<String>& arr, String separator, Allocator* string_allocator )
{
    assert(string_allocator != nullptr);

    // init a string to store each data with a separator
    size_t temp_size = 0;
    for( size_t i = 0; i < arr.size; i++)
        temp_size += arr.at(i).size;
    if( separator.size > 0 && arr.size > 1)
        temp_size += separator.size * (arr.size-1); // 1 separator after each, except last

    String result{string_allocator};
    result.init(temp_size);
    
    assert(result.data != nullptr);
    char* cursor = result.data;
    for(size_t i = 0; i < arr.size; ++i)
    {
        if ( i && separator.size )
        {
            memcpy(cursor, separator.data, separator.size);
            cursor += separator.size;
            LOG_DEBUG("%s\n", result.data);
        }
        char* dst = arr.at(i).data;
        assert(dst != nullptr);
        memcpy(cursor, dst, arr.at(i).size);
        cursor += arr.at(i).size;
        LOG_DEBUG("%s\n", result.data);
    }
    LOG_DEBUG("%s\n", result.data);

    return result;
}

now::StringBuilder& now::StringBuilder::append(const char* str)
{
    append(String{str});
    return *this;
}

now::StringBuilder& now::StringBuilder::append(String str)
{
    data.append(str);
    return *this;
}

template<typename T>
now::StringBuilder& now::StringBuilder::append(const Array<T>& arr)
{
    for(size_t i = 0; i < arr.size; ++i)
    {
        append( arr.at(i) );
    }
    return *this;
}

now::String now::StringBuilder::build_string(String separator, Allocator* allocator)
{
    return join(this->data, separator, allocator);
}

void now::log_message(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

#ifdef NOW_DEBUG
    _flushall();
#endif

}

int now::system(const String& command, const Array<String>& args, bool fatal)
{
    StringBuilder sb{};
    sb.append(command);
    sb.append(args);
    String temp = sb.build_string(" ", temp_allocator() );

    LOG("%s\n", temp.cstr() );
    int code = std::system( temp.data );
    if (code && fatal) LOG("-- ERR: Unable to run: %s\n", temp.data );
    return code;
}

void now::remove(const String& path)
{
    std::filesystem::remove(path.cstr());
}

void now::rename(const String& src, const String& dst)
{
    std::filesystem::rename(src.cstr(), dst.cstr());
}

int now::mkdir_p(const String& path)
{
#if __unix__ or __DARWIN__
    return now::system( "mkdir", { "-p", path.c_str()} );
#else
    return now::system( "mkdir", Array{ path } );
#endif
}

bool now::exists(const String& path)
{
    return std::filesystem::exists(path.cstr());
}


now::State& now::get_state()
{
    static now::State s_state;
    return s_state;
}

now::Task* now::new_task(
        State&          state,
        Task::Type      type,
        String          name, 
        Array<String>   deps)
    {
        
    Task& task = state.tasks[name];

    task.type  = type;
    task.name  = name;
    task.deps  = deps;

    return &task;
}

const now::Task* now::find_task(const State& state, const String& name)
{
    // Search existing task
    auto it = state.tasks.find(name);
    if (it != state.tasks.end())
        return &it->second;

    return nullptr;
}

now::Code now::invoke_task(const State& state, const Task* task)
{
    assert(task != nullptr && "Undefined task!");

    // TODO: in case of file tasks, we have to check file's timestamp or hash and flag it done.

    if (task->done)
    {
        LOG_DEBUG("-- Skip task %s\n", task->name.cstr());
        return Code_OK_SKIPPED;
    }

    LOG_DEBUG("-- Invoke task %s\n", task->name.cstr());

    // dependencies
    for (size_t i = 0; i < task->deps.size; ++i)
    {       
        String dep = task->deps.at(i);
        const Task* dep_task = find_task(state, dep);
        if (!dep_task)
        {
            if (exists(dep))
                continue;
                
            LOG("-- ERR: Unable to find dependency '%s'\n", dep.cstr());
            return Code_FAILED;
        }
        else if( invoke_task(state, dep_task) == Code_FAILED )
        {
            return Code_FAILED;
        }
    }
    
    
    if (task->action != nullptr)
    {
        task->action(task);
    }
    
    task->done = true;
    LOG_DEBUG("-- Invoke task %s DONE\n", task->name.cstr());

    return Code_OK;
}

void now::reset_state(State& state)
{
    for (auto& [_, task] : state.tasks)
    {
        task.done = false;
    }
}

void now::print_tasks(State& state)
{
    LOG("Tasks:\n");

    for (const auto& [name, task] : state.tasks)
    {
        LOG("\t%s", name.cstr(), 20);
        if ( task.deps.size )
        {
            LOG(": %s", task.desc.cstr() );
        }
        LOG("\n");
    }
}

void now::print_help(now::State& state)
{
    LOG("Usage: %s <task> [<task2>, ...]\n\n", state.binary.basename().cstr() );
    now::print_tasks( state );
}

now::Code now::task_invoke_sequence(const State& state, const std::vector<Task*>& tasks)
{
    for (Task* task : tasks)
    {       
        if ( now::invoke_task(state, task) == Code_FAILED)
        {
            return Code_FAILED;
        }
    }

    return Code_OK;
}

int now::parse_args(int argc, char* argv[])
{   
    State& state = now::get_state();
    state.binary = argv[0];
    
    if (argc == 1)
    {
        print_help(state);
        LOG("\nAt least a task argument is expected.\n");
        return 1;
    }    
    
    for (size_t i = 1; i < argc; ++i)
    {       
        const Task* task = find_task(state, argv[i]);

        if ( task == nullptr )
        {
            print_help(state);
            LOG("\nUnknown task: '%s'\n", task->name.cstr() );
            return 1;
        }

        if( invoke_task(state, task) == Code_FAILED )
        {
            LOG("Failed to run '%s'\n", task->name.cstr() );
            return 1;
        }
    }

    return 0;
}

void now::compile_object(String src)
{
    Array<String> obj = { BUILD_DIR, "/", src.stem(), ".o" };
    now::system( COMPILER, { CXXFLAGS, "-c", src.cstr(), "-o", join(obj).cstr() } );

}

void now::link(String binary, Array<String>& objects)
{
    // rename <binary> => <binary>.old
    {
        StringBuilder sb;
        sb.append(binary);
        sb.append(".old");

        String binary_old = sb.build_string();

        if (exists(binary_old)) remove(binary_old);
        if (exists(binary))     rename(binary, binary_old);
    }

    Array<String> args;

    args.concat(objects);
    args.append("-o");
    args.append(binary);

    now::system(COMPILER, args);
}

void now::init()
{      
    TASK(tasks){
        print_tasks( get_state() );
    };

    TASK(help) {
         print_help( get_state() );
    };
}

namespace now
{
    struct Dependencies
    {
        bool                    could_not_parse = false;
        now::String             target;
        now::Array<now::String> files;
    };

    Dependencies parse_d_file(const char *filename)
    {
        LOG_DEBUG("Parsing '%s' ...\n", filename);
        Dependencies result;
        FILE *file = fopen(filename, "r");
        
        if (!file)
        {
            LOG_DEBUG("ERR: fopen %s\n", filename);
            result.could_not_parse = true;
            return result;
        }
        
        char curr_line[255];
        if ( fgets(curr_line, sizeof(curr_line), file))
        {
            // Find the colon separator
            char *colon_ptr = strchr(curr_line, ':');
            if ( colon_ptr == nullptr)
            {
                LOG_DEBUG("WARN: Unable to find a ':' colon!\n", filename);
                result.could_not_parse = true;
            }
            else
            {
                // Extract target
                result.target = String::copy(colon_ptr - curr_line, curr_line);
                LOG_DEBUG("Target found: %s\n", result.target.cstr());

                // Parse dependencies
                char *deps_str = colon_ptr + 1;
                char *token    = strtok(deps_str, " \t\n");
                
                while (token != nullptr)
                {
                    String dep = String::copy(token);
                    result.files.append( dep );
                    LOG_DEBUG("Dependency #%i found: %s\n", result.files.size, dep.cstr() );
                    token = strtok(NULL, " \t\n");
                }
            }
        }
        
        fclose(file);
        return result;
    }
};

void now::rebuild_it_self_if_needed(String binary, String source)
{   
#ifndef NOW_ALWAYS_REBUILD

    bool needs_to_rebuild = false;
    Dependencies dependencies = parse_d_file("task.d");    
    auto binary_time = std::filesystem::last_write_time( binary.cstr() );
    for(size_t i = 0; i < dependencies.files.size; i++)
    {        
        auto dependency_time = std::filesystem::last_write_time( dependencies.files.at(i).cstr() );
        needs_to_rebuild |=  binary_time < dependency_time;

        if ( needs_to_rebuild )
        {
            LOG_DEBUG("%s needs to be recompiled, %s changed\n", dependencies.target.cstr(), dependencies.files.at(i).cstr() );
            break;
        }
    }

    if ( needs_to_rebuild || dependencies.could_not_parse )
#endif
    {
        // Rename current binary (we can't overwrite it while running, but we can rename it)
        now::rename(binary, join({binary, ".old"}, temp_allocator() ).cstr() );

        // Compiles
        Array<String> args = {
            CXXFLAGS,
            "-D_CRT_SECURE_NO_WARNINGS",
            "-g -O0",
            "task.cpp",
            "-o",
            binary,
            "-MMD", "-MF", "task.d"
        };
        now::system(COMPILER, args);

        // Run again and exit (we don't want to run the tasks twice!)
        int code = now::system(binary);
        exit(code);
    }

    #ifdef NOW_ENABLE_TESTS
        run_tests();
    #endif
}

#endif

#ifdef NOW_ENABLE_TESTS
void now::run_tests()
{
    LOG("Running tests ..\n");

    // String
    {
        String str;
        assert( str.allocator == heap_allocator() );
    }

    {
        String str{"Hello"};
        assert( str.allocator == nullptr );
    }

    {
        String str = "Hello";
        assert( str.allocator == nullptr );
    }

    {
        String str = "Hello";
        assert( strcmp( str.cstr(), "Hello") == 0);
    }

    // Array
    {
        Array<String> arr;
        assert( arr.allocator == heap_allocator() );
    }

    {
        Array<String> arr2{"A", "B", "C"};
        assert( arr2.size == 3 );
        arr2.release();
        assert(arr2.size == 0);
    }

    LOG("Tests DONE\n");
}
#endif
