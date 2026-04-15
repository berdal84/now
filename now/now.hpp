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
#include <unordered_set>
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
    void   log_message(const char *format, ...);

    struct Allocator
    {
        Allocator()  {}
        virtual ~Allocator() {};
        virtual void* malloc(size_t size) = 0;
        virtual void  free(void* ptr) = 0;
        virtual void* realloc(void* ptr, size_t size) = 0;
    };

    static Allocator* default_allocator();
    static Allocator* temp_allocator();

    struct NullAllocator : public Allocator
    {
        ~NullAllocator() = default;

        void* malloc(size_t size) override
        { return nullptr; }

        void free(void* ptr) override
        {}

        void* realloc(void* ptr, size_t size) override
        { return nullptr; }
    };

#ifdef NOW_DEBUG_MEMORY
    struct Allocation_Tracker
    {
        const char* name = "default";

        struct Allocation
        {
            void*  address;
            size_t size;
        };

        void add_allocation(void* ptr, size_t size)
        {   
            assert( find_allocation(ptr) == nullptr && "Can't add same address twice!");
            Allocation alloc;
            alloc.address = ptr;
            alloc.size    = size;
            allocations.push_back(alloc);

            LOG_DEBUG("[mem:%s] add %p (size: %lu)\n", name, ptr, size);
        }

        const Allocation* find_allocation(void* ptr) const
        {
            for(auto& each : allocations)
                if (each.address == ptr)
                    return &each;
            return nullptr;
        }

        void remove_allocation(void* ptr)
        {                       
            auto it = std::find_if(
                allocations.begin(), allocations.end(),
                [ptr](auto& item){ return item.address == ptr; }
            );
            assert(it != allocations.end() && "Can't find any allocation with this address!"); 
            allocations.erase(it);
            LOG_DEBUG("[mem:%s] remove %p (size: %lu)\n", name, it->address, it->size);
        }

        std::vector<Allocation> allocations; // TODO: use a performant container when it will get too slow (unordered set, or something non std)
    };
#endif

    struct HeapAllocator : public Allocator
    {
#ifdef NOW_DEBUG_MEMORY
        Allocation_Tracker tracker = {"heap"};
#endif
        ~HeapAllocator() = default;

        void* malloc(size_t size) override
        {
            void* ptr = std::malloc(size);

            #ifdef NOW_DEBUG_MEMORY
                tracker.add_allocation(ptr, size);
            #endif
            
            return ptr;
        }

        void free(void* ptr) override
        {
            #ifdef NOW_DEBUG_MEMORY
                tracker.remove_allocation(ptr);
            #endif

            return std::free(ptr);
        }

        void* realloc(void* ptr, size_t size) override
        {
            #ifdef NOW_DEBUG_MEMORY
                tracker.remove_allocation(ptr);
                tracker.add_allocation(ptr, size);
            #endif

            return std::realloc(ptr, size);
        }

    };

    template<size_t SIZE_IN_BYTES>
    struct RingBufferAllocator : public Allocator
    {
        char   buffer[SIZE_IN_BYTES];
        size_t next_pos = {0};

        #ifdef NOW_DEBUG_MEMORY
        Allocation_Tracker tracker = {"ring_buffer"};
        #endif

        void* malloc(size_t size) override
        {
            void* ptr = _acquire(size);
            
            return ptr;
        }

        void free(void* ptr) override
        {
            #ifdef NOW_DEBUG_MEMORY
                tracker.remove_allocation(ptr);
            #endif
        }

        void* realloc(void* ptr, size_t size) override
        {
            #ifdef NOW_DEBUG_MEMORY
                tracker.remove_allocation(ptr);
                tracker.add_allocation(ptr, size);
            #endif

            // TODO: reuse existing space
            //       We could store the latest aquired ptr, and if it existing_ptr == ptr we could simply extend
            return _acquire(size);
        }

        char* _acquire(size_t size)
        {
            assert( size < SIZE_IN_BYTES/2 && "Increase RingBuffer!");

            if ( size == 0 )
            {
                return nullptr;
            }

            char* result = buffer + next_pos;
            next_pos = (next_pos + size) % SIZE_IN_BYTES;
            return result;
        }
    };

    static Allocator* default_allocator()
    {
        static HeapAllocator heap_allocator;
        return &heap_allocator;
    }

    static Allocator* temp_allocator()
    { 
        static RingBufferAllocator<5*1024*1024> ring_buffer;
        return &ring_buffer;
    }

    struct String
    {
        static constexpr Allocator* literal_allocator_placeholder = nullptr;
        static constexpr size_t     invalid_index = (size_t)-1;

        size_t      size      = {0};
        char*       data      = {nullptr};
        Allocator*  allocator = {nullptr};

        String()
        {
            allocator = default_allocator();
        }

        String(Allocator* _allocator)
        : allocator(_allocator)
        {
            assert(_allocator != nullptr);
        }

        String(const char* str)
        : size(strlen(str))
        , data(const_cast<char*>(str))
        , allocator(literal_allocator_placeholder)
        {}

        String(size_t _size, char* _data)
        : size(_size)
        , data(_data)
        {
            allocator = default_allocator();
        }

#define NOW_STD_COMPATIBILITY
#ifdef NOW_STD_COMPATIBILITY
        explicit String(const std::string& str)
        : size(str.size())
        , data(const_cast<char*>(str.data()))
        {}
#endif

        void init(size_t _size)
        {
            assert(data == nullptr);
            assert(allocator != nullptr);
            size = _size;

            #ifdef NOW_DEBUG_MEMORY
                data = reinterpret_cast<char*>(allocator->malloc(_size+4));
                std::memset(data, 'i', _size*sizeof(char)); // to help debugging
                memcpy(data + size, "END\0", 4);
            #else
                data = reinterpret_cast<char*>(allocator->malloc(_size));
            #endif
        }

        void free()
        {
            assert( allocator != nullptr );
            allocator->free(data);
            data = nullptr;
        }

        char operator[](size_t pos)
        { assert(pos < size && "out of bounds"); return data[pos]; }

        size_t rfind(char c)
        {
            size_t pos = size-1;
            while ( pos != String::invalid_index && data[pos] != c)
            {
                --pos;
            }
            return pos;
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

        String stem()
        {
            size_t index = rfind('.');
            if( index == invalid_index )
                return *this;
            return lsplit(index);
        }

        char* cstr() const // TODO: RingBuffer should be generic (usr virtuals or delegates)
        {
            if (allocator == literal_allocator_placeholder)
            {
                return data;
            }

            size_t cstr_size = size+1;
            char* ptr = reinterpret_cast<char*>( temp_allocator()->malloc(cstr_size) ); // +1 for null termination
            std::memcpy(ptr, data, size);
            ptr[cstr_size-1] = 0;
            return ptr;
        }

        static String copy(size_t source_size, char* source_data)
        { return String::copy(String{source_size, source_data}); }

        static String copy(const String source)
        {
            String result;
            result.init(source.size);
            std::memcpy(result.data, source.data, source.size);
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

        size_t      size        = 0;
        T*          data        = nullptr;
        Allocator*  allocator   = nullptr;
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
            resize(list.size());
            std::copy(list.begin(), list.end(), data);
        }

        ~Array()
        {
        }

        void free()
        {
            assert(allocator != nullptr);
            allocator->free(data);
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
                    data = reinterpret_cast<T*>(allocator->malloc(new_size));

                    for( size_t i=0; i < new_size; i++)
                        new (data+i) T(); // construct in-place
                }
                else
                {
                    T*     old_data = data;
                    size_t old_size = size;

                    assert(allocator != nullptr);
                    data = reinterpret_cast<T*>(allocator->realloc(data, new_size));
                    std::copy(old_data, old_data+old_size, data);
                    
                    for( size_t i=old_size; i < new_size; i++)
                        new (data+i) T(); // construct in-place
                }
            }
            //capacity = new_size; // Currently capacity == size, but that's temporary.
            size = new_size;
        }

        void append(T str)
        {
            size_t index = size;
            resize( index + 1 );
            data[index] = str;
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
        { assert(pos < size && "out of bounds"); return *(data + pos); }

        T& at(size_t pos)
        { assert(pos < size && "out of bounds"); return *(data + pos); }
    };

    struct StringBuilder
    {
        Array<String> data;

        StringBuilder()
        : data()
        {
            data.allocator = temp_allocator();
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
        String          build_string(String separator = "", Allocator* allocator = temp_allocator() );
    };

    int     system(const String& command, const Array<String>& args= {}, bool fatal = true);
    void    remove(const String& path);
    void    rename(const String& src, const String& dst);
    int     mkdir_p(const String& path);
    bool    exists(const String& path);
    String  join(const Array<String>& arr, String separator = "", Allocator* string_allocator = temp_allocator() );

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
        append(arr.data[i]);
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
}

int now::system(const String& command, const Array<String>& args, bool fatal)
{
    StringBuilder sb;
    sb.append(command);
    sb.append(args);
    const char* temp = sb.build_string(" ").cstr();

    LOG("%s\n", temp);
    int code = std::system( temp );
    if (code && fatal) LOG("-- ERR: Unable to run: %s\n", temp);
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
        LOG_DEBUG("-- Skip task %s\n", task->name);
        return Code_OK_SKIPPED;
    }

    LOG_DEBUG("-- Invoke task %s\n", task->name);

    // dependencies
    for (size_t i = 0; i < task->deps.size; ++i)
    {       
        String dep = task->deps.at(i);
        const Task* dep_task = find_task(state, dep);
        if (!dep_task)
        {
            if (exists(dep))
                continue;
                
            LOG("-- ERR: Unable to find dependency '%s'\n", dep);
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
    LOG_DEBUG("-- Invoke task %s DONE\n", task->name);

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
        LOG("\t%s", name, 20);
        if ( task.deps.size )
        {
            LOG(": %s", task.desc );
        }
        LOG("\n");
    }
}

void now::print_help(now::State& state)
{
    LOG("Usage: %s <task> [<task2>, ...]\n\n", state.binary);
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
            LOG("\nUnknown task: '%s'\n", task->name);
            return 1;
        }

        if( invoke_task(state, task) == Code_FAILED )
        {
            LOG("Failed to run '%s'\n", task->name);
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
        now::String target;
        now::Array<now::String> deps;
    };

    Dependencies parse_d_file(const char *filename)
    {
        LOG("Parsing %s ...\n", filename);
        Dependencies result;
        FILE *file = fopen(filename, "r");
        
        if (!file)
        {
            LOG("ERR: fopen %s\n", filename);
            return result;
        }
        
        char* curr_line;
        if ( fgets(curr_line, sizeof(curr_line), file))
        {
            // Find the colon separator
            char *colon_ptr = strchr(curr_line, ':');
            if ( colon_ptr == nullptr)
            {
                LOG("WARN: Unable to find a ':' colon!\n", filename);
            }
            else
            {
                // Extract target
                result.target = String::copy(colon_ptr - curr_line, curr_line);
                LOG("Target found: %s\n", result.target.cstr());

                // Parse dependencies
                char *deps_str = colon_ptr + 1;
                char *token    = strtok(deps_str, " \t\n");
                
                while (token != nullptr)
                {
                    String dep = String::copy(token);
                    result.deps.append( dep );
                    LOG("Dependency #%i found: %s\n", result.deps.size, dep.cstr() );
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
    bool needs_to_rebuild = std::filesystem::last_write_time(binary.cstr()) < std::filesystem::last_write_time(source.cstr()); // TODO: uses *.d files, the cpp might include dependencies that may have change.

    //if (needs_to_rebuild)
    {
        // Rename current binary (we can't overwrite it while running, but we can rename it)
        Array<String> arr{binary, ".old"};
        now::rename(binary, join(arr, temp_allocator()).cstr() );

        // Compiles
        Array<String> args = {
            CXXFLAGS,
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
}

#endif
