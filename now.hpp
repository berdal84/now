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
    struct String
    {
        static constexpr size_t invalid_index = (size_t)-1;

        size_t size;
        char*  data;

        String()
        : size(0)
        , data(nullptr)
        {}

        String(const char* str)
        : size(strlen(str))
        , data(const_cast<char*>(str))
        {
        }

        String(size_t _size, char* _data)
        : size(_size)
        , data(_data)
        {}

#ifdef NOW_STD_STRING
        explicit String(const std::string& str)
        : size(str.size())
        , data(const_cast<char*>(str.data()))
        {}
#endif

        void init(size_t _size)
        {
            assert(data == nullptr);
            size = _size;
            size_t capacity = _size+1; // +1 for null terminator to be cstr compatible
            data = new char[capacity];
            data[capacity-1] = 0;
        }

        void free()
        {
            delete[] data;
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

        const char* c_str() const
        { return data; /* on init, we reserve an extra char for a '\0' to be C-string compatible */}
    };

    template<typename T>
    struct Array
    {
        static constexpr size_t invalid_index = (size_t)-1;
        size_t size     = 0;
        T*     data     = nullptr;
        size_t capacity = 0;
        
        Array() = default;

        Array(std::initializer_list<T> init)
        {
            resize(init.size());
            std::copy(init.begin(), init.end(), data);
        }

        void free()
        {
            delete data;
        }

        void resize(size_t new_size)
        {
            assert(new_size >= size);

            // ensure has capacity
            if( new_size > capacity )
            {
                if( data == nullptr )
                {
                    data = new T[new_size];
                }
                else
                {
                    T* old_data = data;
                    data = new T[new_size];
                    std::copy(old_data, old_data+size, data);
                    delete[] old_data;
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

        const T& operator[](size_t pos) const
        { assert(pos < size && "out of bounds"); return *(data + pos); }

        T& operator[](size_t pos)
        { assert(pos < size && "out of bounds"); return *(data + pos); }
   
        String join(const char* separator = "") const;
    };

    struct StringBuilder
    {
        Array<String> data;
        String        temp;

        StringBuilder() {}

        StringBuilder& append(const char* str);
        StringBuilder& append(String str);

        template<typename T>
        StringBuilder& append(const Array<T>& arr);
        
        String join(String separator = "");
    };

    void    log_message(const char *format, ...);
    int     system(Array<const char*> command, bool fatal = true);
    int     system(const char* command, bool fatal = true);
    void    remove(const char* path);
    void    rename(const char* src, const char* dst);
    int     mkdir_p(const char* path);
    bool    exists(const char* path);

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
        const char*         name = "";
        const char*         desc = "";
        Array<const char*>  deps;
        Action              action = &null_action;
    };

    struct State
    {
        struct StringHash
        {
            using is_transparent = void;
            size_t operator()(const char* s) const   { return std::hash<std::string_view>{}(s); }
        };

        struct StringEqual
        {
            using is_transparent = void;
            bool operator()(const char* a, const char* b) const    { return strcmp(a, b) == 0; }
        };

        String binary;
        std::unordered_map<const char*, Task, StringHash, StringEqual> tasks;
    };

    static State&       get_state();
    static Task*        new_task( State& state, Task::Type type, const char* name, Array<const char*> deps);
    static const Task*  find_task(const State& state, const char* name);
    static Code         invoke_task(const State& state, const Task* task);
    static void         reset_state(State& state);
    static void         print_tasks(State& state);
    static void         print_help(State& state);
    static Code         task_invoke_sequence(const State& state, const std::vector<Task*>& tasks);
    static int          parse_args(int argc, char* argv[]);
    static void         compile_object(now::String src);
    static void         link(const char* binary, now::Array<const char*>& objects);
    static void         init();
    static void         rebuild_it_self(const char* binary, const char* source);
}

#ifdef NOW_IMPLEMENTATION

template <typename T>
now::String now::Array<T>::join(const char* separator) const
{
    StringBuilder sb;
    for(size_t i = 0; i < size; i++)
    {
        sb.append( data[i] );
    }
    return sb.join(separator);
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

now::String now::StringBuilder::join(String separator) // c_str compatible
{
    // init a string to store each data with a separator
    size_t str_size = 0;
    for( size_t i = 0; i < data.size; i++)
        str_size += data[i].size;
    if( separator.size > 0 && data.size > 1)
        str_size += separator.size * (data.size-1); // 1 separator after each, except last

    temp.init(str_size);

    char* cursor = temp.data;
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
    return temp;
}

void now::log_message(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

int now::system(Array<const char*> cmd_args, bool fatal)
{
    String cmd_str = cmd_args.join(" ");
    int code = now::system(cmd_str.c_str(), fatal);
    cmd_str.free();
    return code;
}

int now::system(const char* command, bool fatal)
{
    LOG("%s\n", command);
    int code = std::system(command);
    if (code && fatal) LOG("-- ERR: Unable to run: %s\n", command);
    return code;
}

void now::remove(const char* path)
{
    std::filesystem::remove(path);
}

void now::rename(const char* src, const char* dst)
{
    std::filesystem::rename(src, dst);
}

int now::mkdir_p(const char* path)
{
    StringBuilder sb;
    sb.append("mkdir");

#if __unix__ or __DARWIN__
    sb.append("-p");
#endif

    sb.append(path);
    String cmd = sb.join();
    int code = now::system( cmd.data );
    return code;
}

bool now::exists(const char* path)
{
    return std::filesystem::exists(path);
}


now::State& now::get_state()
{
    static now::State s_state;
    return s_state;
}

now::Task* now::new_task(
        State&              state,
        Task::Type          type,
        const char*         name, 
        Array<const char*>  deps)
    {
        
    Task& task = state.tasks[name];

    task.type  = type;
    task.name  = name;
    task.deps  = deps;

    return &task;
}

const now::Task* now::find_task(const State& state, const char* name)
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
        const char* dep = task->deps[i];
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
    StringBuilder sb;

    sb.append(COMPILER);
    sb.append(CXXFLAGS); 
    sb.append("-c");
    sb.append(src);
    sb.append("-o");

    StringBuilder sb2;
    sb2.append(BUILD_DIR);
    sb2.append("/");
    sb2.append(src.stem());
    sb2.append(".o");
    String obj = sb2.join();
    sb.append(obj);

    String cmd = sb.join(" ");
    now::system( cmd.data );

    cmd.free();
    obj.free();
}

void now::link(const char* binary, Array<const char*>& objects)
{
    // rename <binary> => <binary>.old
    {
        StringBuilder sb;
        sb.append(binary);
        sb.append(".old");

        String binary_old = sb.join();

        if (exists(binary_old.data)) remove(binary_old.data);
        if (exists(binary))     rename(binary, binary_old.data);
        
        binary_old.free();
    }

    StringBuilder sb;

    sb.append(COMPILER);

    for (size_t i = 0; i < objects.size; ++i )
    {
        sb.append(objects[i]);
    }

    sb.append("-o");
    sb.append(binary);

    String cmd = sb.join(" ");
    now::system( cmd.data );
    cmd.free();
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

void now::rebuild_it_self(const char* binary, const char* source)
{   
    bool needs_to_rebuild = std::filesystem::last_write_time(binary) < std::filesystem::last_write_time(source);

    if (needs_to_rebuild)
    {
        Array<const char*> build_command = {
            COMPILER,
            CXXFLAGS,
            "-g -O0",
            "task.cpp",
            "-o",
            binary
        };

        StringBuilder sb;
        sb.append(binary);
        sb.append(".old");
        String binary_old = sb.join();
        now::rename(binary, binary_old.c_str() );
        binary_old.free();
        now::system(build_command);
        int code = now::system(binary);
        exit(code);
    }
}

#endif