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
    struct Allocator
    {
        Allocator()  {}
        ~Allocator() {}

        virtual void* allocate(size_t mem_size) = 0;
        virtual void  deallocate(void* ptr) = 0;
        virtual void* reallocate(void* ptr, size_t size) = 0;
    };

    static Allocator* default_allocator();
    static Allocator* temp_allocator();

    struct NullAllocator : public Allocator
    {
        void* allocate(size_t size) override
        { return nullptr; }

        void deallocate(void* ptr) override
        {}

        void* reallocate(void* ptr, size_t size) override
        { return nullptr; }
    };

    struct HeapAllocator : public Allocator
    {
        void* allocate(size_t size) override
        { return std::malloc(size); }

        void deallocate(void* ptr) override
        { std::free(ptr); }

        void* reallocate(void* ptr, size_t size) override
        { return std::realloc(ptr, size); }
    };

    struct RingBuffer
    {
        char*  data;
        size_t size;
        size_t next;

        RingBuffer(size_t size_in_bytes)
        : data(nullptr)
        , size(size_in_bytes)
        , next(0)
        {
            init(size_in_bytes);
        }

        ~RingBuffer()
        {
            shutdown(); // We don't really need to do this, since this ring buffer should live for the entire execution of he program
        }

        void shutdown()
        {
            if (data)
            {
                default_allocator()->deallocate(data);
                data = nullptr;
            }
        }

        void init(size_t size_in_bytes)
        {
            data = static_cast<char*>(default_allocator()->allocate(size_in_bytes));
            assert(data);
            std::memset(data, 0, size); // safer to zero-initialize
        }

        char* acquire(size_t mem_size)
        {
            assert(data != nullptr);
            assert(mem_size < size);

            if (mem_size == 0)
            {
                return nullptr;
            }

            char* result = &data[next];
            next = (next + mem_size) % size;
            return result;
        }
    };

    struct RingBufferAllocator : public Allocator
    {
        RingBuffer& buffer;

        RingBufferAllocator(RingBuffer& buffer)
        : buffer(buffer)
        {}

        ~RingBufferAllocator()
        { /* nothing to do for a ring buffer */ }

        void* allocate(size_t size) override
        { return buffer.acquire(size); }

        void deallocate(void* ptr) override
        { /* nothing to do for a ring buffer */ }

        void* reallocate(void* existing_ptr, size_t size) override
        {
            // TODO: reuse existing space
            //       We could store the latest aquired ptr, and if it existing_ptr == ptr we could simply extend
            return buffer.acquire(size);
        }
    };
    
    static Allocator* default_allocator()
    {
        static HeapAllocator heap_allocator;
        return &heap_allocator;
    }

    static Allocator* temp_allocator()
    {
        static RingBuffer          ring_buffer{5 * 1024 * 1024};
        static RingBufferAllocator ring_buffer_allocator{ring_buffer};
        return &ring_buffer_allocator;
    }

    struct String
    {
        static constexpr size_t invalid_index = (size_t)-1;

        size_t      size;
        char*       data;
        Allocator*  allocator;

        String(Allocator* _allocator = default_allocator() )
        : size(0)
        , data(nullptr)
        , allocator(_allocator)
        {}

        String(const char* str, Allocator* _allocator = default_allocator() )
        : size(strlen(str))
        , data(const_cast<char*>(str))
        , allocator(_allocator)
        {}

        String(size_t _size, char* _data, Allocator* _allocator = default_allocator() )
        : size(_size)
        , data(_data)
        , allocator(_allocator)
        {}

#define NOW_STD_COMPATIBILITY
#ifdef NOW_STD_COMPATIBILITY
        explicit String(const std::string& str)
        : size(str.size())
        , data(const_cast<char*>(str.data()))
        {}

        operator std::filesystem::path ()
        { return { data, data+size }; }
#endif
        void init(size_t _size, Allocator* allocator = default_allocator())
        {
            assert(data == nullptr);
            size        = _size;
            data        = static_cast<char*>(allocator->allocate(_size));
            allocator   = allocator;
        }

        void free()
        {
            allocator->deallocate(data);
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
            return String{ index, data, allocator };
        }

        String rsplit(size_t index)
        {
            assert(index < size && "Out of bounds");
            return String{ size - index, data + index, allocator };
        }

        String stem()
        {
            size_t index = rfind('.');
            if( index == invalid_index )
                return *this;
            return lsplit(index);
        }

        char* cstr(Allocator* allocator = temp_allocator() ) const // TODO: RingBuffer should be generic (usr virtuals or delegates)
        {
            size_t cstr_size = size+1;
            char* ptr = static_cast<char*>( allocator->allocate(cstr_size) ); // +1 for null termination
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
        size_t      capacity    = 0;
        
        Array(Allocator* _allocator = default_allocator())
        : size(0)
        , data(nullptr)
        , allocator(_allocator)
        , capacity(0)
        {}

        Array(std::initializer_list<T> list)
        : size(0)
        , data(nullptr)
        , allocator(default_allocator())
        , capacity(0)
        {
            resize(list.size());
            std::copy(list.begin(), list.end(), data);
        }

        void free()
        {
            allocator->deallocate(data);
            data     = nullptr;
            size     = 0;
            capacity = 0;
        }

        void resize(size_t new_size)
        {
            assert(new_size >= size);

            // ensure has capacity
            if( new_size > capacity )
            {
                if( data == nullptr )
                {
                    data = static_cast<T*>(allocator->allocate(new_size));

                    for( size_t i=0; i < new_size; i++)
                        new (data+i) T(); // construct in-place
                }
                else
                {
                    T*     old_data = data;
                    size_t old_size = size;

                    data = static_cast<T*>(allocator->reallocate(data, new_size));
                    std::copy(old_data, old_data+old_size, data);

                    for( size_t i=old_size; i < new_size; i++)
                        new (data+i) T(); // construct in-place
                }
            }

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
                (*this)[old_size+i] = other[i];
            }
        }

        const T& operator[](size_t pos) const
        { assert(pos < size && "out of bounds"); return *(data + pos); }

        T& operator[](size_t pos)
        { assert(pos < size && "out of bounds"); return *(data + pos); }
   
        String join(const char* separator = "", Allocator* allocator = default_allocator() ) const;
    };

    struct StringBuilder
    {
        Array<String> data;

        StringBuilder(Allocator* allocator = default_allocator())
        : data(allocator)
        {}

        StringBuilder&  append(const char* str);
        StringBuilder&  append(String str);
        template<typename T>  
        StringBuilder&  append(const Array<T>& arr);              
        String          join(String separator = "", Allocator* allocator = default_allocator() );
    };

    void    log_message(const char *format, ...);
    int     system(String command, Array<String> args= {}, bool fatal = true);
    void    remove(String path);
    void    rename(String src, String dst);
    int     mkdir_p(String path);
    bool    exists(String path);

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
            size_t operator()(String s) const   { return std::hash<std::string_view>{}( s.cstr(temp_allocator())); }
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
    static void         rebuild_it_self_if_needed(const String& binary, const String& source);
}

#ifdef NOW_IMPLEMENTATION

template <typename T>
now::String now::Array<T>::join(const char* separator, Allocator* allocator) const
{
    StringBuilder sb;
    for(size_t i = 0; i < size; i++)
    {
        sb.append( data[i] );
    }
    return sb.join(separator, allocator);
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

now::String now::StringBuilder::join(String separator, Allocator* allocator)
{
    // init a string to store each data with a separator
    size_t temp_size = 0;
    for( size_t i = 0; i < data.size; i++)
        temp_size += data[i].size;
    if( separator.size > 0 && data.size > 1)
        temp_size += separator.size * (data.size-1); // 1 separator after each, except last

    String result;
    result.init(temp_size, allocator);
    result.data[temp_size-1] = 0;
    
    char* cursor = result.data;
    for(size_t i = 0; i < data.size; ++i)
    {
        if ( i && separator.size )
        {
            memcpy(cursor, separator.data, separator.size);
            cursor += separator.size;
            //LOG("%s\n", temp.data);
        }
        memcpy(cursor, data[i].data, data[i].size);
        cursor += data[i].size;
        //LOG("%s\n", temp.data);
    }
    //LOG("%s\n", temp.data);

    data.free();

    return result;
}

void now::log_message(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

int now::system(String command, Array<String> args, bool fatal)
{
    StringBuilder sb;
    sb.append(command);
    sb.append(args);
    String temp_str = sb.join(" ");
    LOG("%s\n", temp_str.cstr());
    int code = std::system(temp_str.cstr());
    if (code && fatal) LOG("-- ERR: Unable to run: %s\n", temp_str.cstr());
    return code;
}

void now::remove(String path)
{
    std::filesystem::remove(path);
}

void now::rename(String src, String dst)
{
    std::filesystem::rename(src, dst);
}

int now::mkdir_p(String path)
{
#if __unix__ or __DARWIN__
    return now::system( "mkdir", { "-p", path.c_str()} );
#else
    return now::system( "mkdir", Array{ path } );
#endif
}

bool now::exists(String path)
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
        String dep = task->deps[i];
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
    now::system( COMPILER, { CXXFLAGS, "-c", src.cstr(), "-o", obj.join().cstr() } );

}

void now::link(String binary, Array<String>& objects)
{
    // rename <binary> => <binary>.old
    {
        StringBuilder sb;
        sb.append(binary);
        sb.append(".old");

        String binary_old = sb.join();

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

void now::rebuild_it_self_if_needed(const String& binary, const String& source)
{   
    bool needs_to_rebuild = std::filesystem::last_write_time(binary.cstr()) < std::filesystem::last_write_time(source.cstr()); // TODO: uses *.d files, the cpp might include dependencies that may have change.

    if (needs_to_rebuild)
    {
        // Rename current binary (we can't overwrite it while running, but we can rename it)
        const char * new_name = Array<String>{binary, ".old"}.join("", temp_allocator()).cstr();
        now::rename(binary, new_name );

        // Compiles
        now::system(COMPILER, {
            CXXFLAGS,
            //"-g -O0",
            "task.cpp",
            "-o",
            binary,
            "-MMD", "-MF", "task.d"
        });

        // Run again and exit (we don't want to run the tasks twice!)
        int code = now::system(binary);
        exit(code);
    }
}

#endif
