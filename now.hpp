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

//-----------------------------------------------------------------------------
// MACROS
// ----------------------------------------------------------------------------

#define LOG(FMT, ...) now::log_message(FMT, __VA_ARGS__ )

#ifdef NOW_VERBOSE
#   define LOG_DEBUG(FMT, ...) now::log_message("[debug] " FMT, __VA_ARGS__ )
#else
#   define LOG_DEBUG(FMT, ...)
#endif

#define REBUILD_ON_CHANGE( COMMAND )\
if( now::system( COMMAND ) )\
{\
    LOG("Unable to rebuild ");\
}\
LOG("Rebuilt %s\n", NOW_PROGRAM);

// Helper to define a task (globally scoped)
#define TASK( TASK_NAME, ...) \
    static const char* TASK_NAME = #TASK_NAME; \
    now::new_task( \
        now::get_state(), \
        now::Task::Type_TASK,\
        #TASK_NAME, \
        {__VA_ARGS__}\
    )->action = [](const now::Task* task) -> void

#define FILETASK( FILE_NAME, ...) \
    now::new_task( \
        now::get_state(), \
        now::Task::Type_FILE,\
        FILE_NAME, \
        {__VA_ARGS__}\
    )->action = [](const now::Task* task) -> void

#define NOW_STATIC_INITIALIZER \
    auto now_static_initializer = []() -> int {

#define NOW_STATIC_INITIALIZER_END \
        return 1; \
    }();

//-----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

namespace now
{

struct String
{
    static constexpr size_t npos = (size_t)-1;

    size_t size;
    char*  data;

    String(size_t _size, char* _data)
    : size(_size)
    , data(_data)
    {}

    String(const std::string& str)
    : size(str.size())
    , data(const_cast<char*>(str.data()))
    {}

    String(const char* str)
    : size(strlen(str))
    , data(const_cast<char*>(str))
    {}

    char operator[](size_t pos)
    { assert(pos < size && "out of bounds"); return data[pos]; }

    size_t rfind(char c)
    {
        size_t pos = size-1;
        while ( pos != String::npos && data[pos] != c)
        {
            --pos;
        }
        return pos;
    }

    String lsplit(size_t pos)
    {
        assert(pos < size && "Out of bounds");
        return String{ pos - size, data };
    }

    String rsplit(size_t pos)
    {
        assert(pos < size && "Out of bounds");
        return String{ size - pos, data + pos };
    }

    String stem()
    {
        size_t pos = rfind('.');
        if( pos == npos )
            return *this;
        return lsplit(pos);
    }
};

struct StringBuilder
{
    std::vector<String> data;
    std::string         result;

    StringBuilder()
    {}

    void append(const char* str)
    {
        data.push_back({str});
    }

    void append(String str)
    {
        data.push_back(str);
    }
    
    StringBuilder& join(const char* sep = "")
    {
        result.resize(0);
        result.reserve(256);
        for(auto it = data.begin(); it != data.end(); ++it)
        {
            if (it != data.begin())
                result += sep;
            result.append(it->data, it->size);
        }
        return *this;
    }

    const char* join_to_temp_cmd(const char* sep = " ")
    {
        return join(sep).result.c_str();
    }

    const char* join_to_temp_cstr(const char* sep = "")
    {
        return join(sep).result.c_str();
    }
};

void log_message(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

int system(const char* command, bool fatal = true)
{
    LOG("%s\n", command);
    int code = std::system(command);
    if (code && fatal) LOG("-- ERR: Unable to run: %s\n", command);
    return code;
}

int mkdir_p(const char* path)
{
    StringBuilder sb;
    sb.append("mkdir");

#if __unix__ or __DARWIN__
    sb.append("-p");
#endif

    sb.append(path);
    return now::system( sb.join_to_temp_cmd() );
}

bool file_exists(const char* path)
{
    return std::filesystem::exists(path);
}

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
        Type_TASK = 1,
        Type_FILE = 2
    };

    using Action = void(*)(const Task*);
    static void null_action(const Task*) {}

    mutable bool                done = false;
    Type                        type = Type_NULL;
    const char*                 name = "";
    const char*                 desc = "";
    std::vector<const char*>    deps;
    Action                      action = &null_action;
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

    const char* binary = "";
    std::unordered_map<const char*, Task, StringHash, StringEqual> tasks;
};

static State& get_state()
{
    static State s_state;
    return s_state;
}

Task* new_task(
        State&      state,
        Task::Type  type,
        const char* name, 
        const std::vector<const char*>& deps)
    {
        
    Task& task = state.tasks[name];

    task.type  = type;
    task.name  = name;
    task.deps  = deps;

    return &task;
}

const Task* find_task(const State& state, const char* name)
{
    // Search existing task
    auto it = state.tasks.find(name);
    if (it != state.tasks.end())
        return &it->second;

    return nullptr;
}

Code invoke_task(const State& state, const Task* task)
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
    for (const char* dep : task->deps)
    {       
        const Task* dep_task = find_task(state, dep);
        if (!dep_task)
        {
            if (file_exists(dep))
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

void reset_state(State& state)
{
    for (auto& [_, task] : state.tasks)
    {
        task.done = false;
    }
}

void print_tasks(State& state)
{
    LOG("Tasks:\n");

    for (const auto& [name, task] : state.tasks)
    {
        LOG("\t%s", name, 20);
        if (!task.deps.empty())
        {
            LOG(": %s", task.desc );
        }
        LOG("\n");
    }
}

void print_help(State& state)
{
    LOG("Usage: %s <task> [<task2>, ...]\n\n", state.binary);
    print_tasks( state );
}

inline Code task_invoke_sequence(const State& state, const std::vector<Task*>& tasks)
{
    for (Task* task : tasks)
    {       
        if ( invoke_task(state, task) == Code_FAILED)
        {
            return Code_FAILED;
        }
    }

    return Code_OK;
}

using namespace now;

int main(int argc, char* argv[])
{   
    State& state = get_state();
    String program_path = argv[0];
    state.binary = program_path.stem().data;

    TASK(tasks){
        print_tasks( get_state() );
    };

    TASK(help) {
        print_help( get_state() );
    };
    
    if (argc == 1)
    {
        print_help(state);
        LOG("\nAt least a task argument is expected.\n");
        return 1;
    }    
    
    for (size_t i = 1; i <= argc; ++i)
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

} // namespace
