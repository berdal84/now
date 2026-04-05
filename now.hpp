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

#ifndef NOW_PROGRAM
static_assert(false, "NOW_PROGRAM must be defined!");
#endif

//-----------------------------------------------------------------------------
// MACROS
// ----------------------------------------------------------------------------

#define LOG(FMT, ...) now::log_message(FMT, __VA_ARGS__ )

#define REBUILD_ON_CHANGE( COMMAND )\
if( now::sh( COMMAND ) )\
{\
    LOG("Unable to rebuild ");\
}\
LOG("Rebuilt %s\n", NOW_PROGRAM);

// Helper to define a task (globally scoped)
#define TASK( NAME, ...) \
    static const char* NAME = #NAME; \
    now::new_task( \
        now::get_state(), \
        NAME, \
        {__VA_ARGS__}\
    )->action = [](const now::Task*) -> void

//-----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

namespace now
{

struct StringBuilder
{
    std::vector<const char*> data;
    std::string              result;

    void append(const char* str)
    {
        data.push_back(str);
    }
    
    StringBuilder& join(const char* separator = "")
    {
        result.resize(0);
        result.reserve(256);
        for(auto it = data.begin(); it != data.end(); ++it)
        {
            if (it != data.begin())
                result += separator;
            result += *it;
        }
        return *this;
    }

    StringBuilder& to_command()
    {
        return join(" ");
    }

    const char* c_str() const
    {
        return result.c_str();
    }
};

void log_message(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

int sh(const char* command, bool fatal = false)
{
    int code = std::system(command);
    if (code && fatal) LOG("Unable to run: %s\n", command);
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
    return now::sh( sb.to_command().c_str() );
}

typedef int Code;
enum Code_ {
    Code_FAILED      = 0,
    Code_OK,
    Code_OK_SKIPPED
};

struct Task
{
    using Action = void(*)(const Task*);
    mutable bool                done = false;
    const char*                 name;
    const char*                 desc = "";
    std::vector<const char*>    deps;
    Action                      action = nullptr;
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

    std::unordered_map<const char*, Task, StringHash, StringEqual> tasks;
};

static State& get_state()
{
    static State s_state;
    return s_state;
}

Task* new_task(
        State& state,
        const char* name, 
        const std::vector<const char*>& deps)
    {
        
    Task& task = state.tasks[name];
    task.name  = name;
    task.deps  = deps;

    return &task;
}

const Task* find_task(const State& state, const char* name)
{
    auto it = state.tasks.find(name);
    return it != state.tasks.end() ? &it->second : nullptr;
}

Code invoke_task(const State& state, const Task* task)
{
    assert(task != nullptr && "Undefined task!");

    // TODO: in case of file tasks, we have to check file's timestamp or hash and flag it done.

    if (task->done)
    {
        LOG("Skipping %s ...\n", task->name);
        return Code_OK_SKIPPED;
    }

    LOG("Invoking task %s ...\n", task->name);

    // dependencies
    for (const char* dep : task->deps)
    {       
        const Task* dep_task = find_task(state, dep);
        if (!dep_task)
        {
            LOG("ERR: Unable to find a task for '%s'\n", dep);
            return Code_FAILED;
        }
        
        if( invoke_task(state, dep_task) == Code_FAILED)
        {
            LOG("ERR: Dependency failed to run: '%s'\n", dep);
            return Code_FAILED;
        }
    }
    
    
    if (task->action)
    {
        LOG("Running ", task, "'s action ...");
        task->action(task);
        LOG("Running ", task, "'s action  DONE");
    }
    
    task->done = true;
    LOG("Invoking task %s DONE\n", task->name);

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
    LOG("Available tasks:\n");

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
    LOG("Usage: %s <task> [<task2>, ...]\n\n", NOW_PROGRAM);
    print_tasks( state );
}

inline Code state_find_and_invoke_tasks(const State& state, size_t task_count, char* task_names[])
{
    for (size_t i = 0; i < task_count; ++i)
    {       
        const char* name = task_names[i];
        const Task* dep_task = find_task(state, name);
        if (!dep_task)
        {
            LOG("ERR: Unable to find a task for '%s'\n", name);
            return Code_FAILED;
        }
        
        if( invoke_task(state, dep_task) == Code_FAILED)
        {
            return Code_FAILED;
        }
    }

    return Code_OK;
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

int main(int argc, char* argv[])
{   
    TASK(tasks) {
        now::print_tasks( now::get_state() );
    };

    TASK(help) {
        now::print_help( now::get_state() );
    };

    int task_count = argc < 2 ? 0 : argc - 1;

    State& state = get_state();
    if (task_count == 0)
    {
        print_help(state);
        LOG("ERR: A task argument is expected, see messages above.");
        return 1;
    }

    char** task_vector = argv + 1;
    state_find_and_invoke_tasks( state, task_count, task_vector );

    return 0;
}

} // namespace
