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

#define LOG(FMT, ...)   buildtools::log_message(__FILE__, __VA_ARGS__ )
#define LOG_DEBUG(FMT, ...)   buildtools::log_message(__FILE__ ":%d %s " FMT, __LINE__ , __func__ __VA_OPT__ (,) __VA_ARGS__ )
#define MKDIR_P( path ) system( "mkdir " path )
#define SH( command )   system( command )

namespace buildtools
{

void log_message(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

typedef int RetCode;
enum RetCode_ {
    RetCode_FAILED      = 0,
    RetCode_OK,
    RetCode_OK_SKIPPED
};

struct Task
{
    bool done = false;
    std::string name;
    std::vector<std::string> deps;
    std::function<void(Task*)> action;
};

struct StringHash
{
    using is_transparent = void;
    
    size_t operator()(std::string_view sv) const
    {
        return std::hash<std::string_view>{}(sv);
    }
    
    size_t operator()(const std::string& s) const
    {
        return std::hash<std::string>{}(s);
    }
};

struct StringEqual
{
    using is_transparent = void;
    
    bool operator()(std::string_view a, std::string_view b) const
    {
        return a == b;
    }
    
    bool operator()(const std::string& a, std::string_view b) const
    {
        return std::string_view(a) == b;
    }
    
    bool operator()(std::string_view a, const std::string& b) const
    {
        return a == std::string_view(b);
    }
    
    bool operator()(const std::string& a, const std::string& b) const
    {
        return a == b;
    }
};

class Registry
{
private:
    std::unordered_map<std::string, Task, StringHash, StringEqual> tasks_;
    
public:
    static Registry& instance()
    {
        static Registry reg;
        return reg;
    }
    
    void register_task(
            std::string_view name, 
            const std::vector<std::string_view>& deps,
            std::function<void(Task*)> action )
        {
        Task task;

        task.name      = std::string(name);
        task.action    = action;

        for (auto d : deps)
        {
            task.deps.emplace_back(d);
        }
        tasks_[task.name] = std::move(task);
    }
    
    Task* find(std::string_view name)
    {
        auto it = tasks_.find(name);
        return it != tasks_.end() ? &it->second : nullptr;
    }
    
    RetCode run_task(std::string_view name);
    
    void clean()
    {
        for (auto& [_, task] : tasks_)
        {
            task.done = false;
        }
    }
    
    void list_tasks() const
    {
        std::cout << "Available tasks:\n";

        for (const auto& [name, task] : tasks_)
        {
            std::cout << "  " << name;
            if (!task.deps.empty())
            {
                std::cout << " => [";
                for (size_t i = 0; i < task.deps.size(); ++i)
                {
                    std::cout << task.deps[i];
                    if (i < task.deps.size() - 1) std::cout << ", ";
                }
                std::cout << "]";
            }
            std::cout << "\n";
        }
    }
    
private:
    RetCode run_task_impl(Task* t);
    RetCode run_deps(const std::vector<std::string>& deps);
};

inline RetCode Registry::run_task_impl(Task* task)
{
    assert(task != nullptr && "Undefined task!");

    if (task->done)
    {
        return RetCode_OK_SKIPPED;
    }
    
    std::cout << "Running task " << task->name << " ...\n";

    if (!task->deps.empty())
    {
        if ( run_deps(task->deps) == RetCode_FAILED )
        {
            return RetCode_FAILED;
        }
    }
    
    
    if (task->action)
    {
        task->action(task);
    }
    
    task->done = true;
    std::cout << "Task " << task->name << " DONE\n";

    return RetCode_OK;
}

inline RetCode Registry::run_deps(const std::vector<std::string>& deps)
{
    for (const auto& dep : deps)
    {
        Task* task = find(dep);
        if ( task == nullptr )
	    {
            std::cerr << "Error: dependency '" << dep << "' not found\n";
            return RetCode_FAILED;
        }
        
        if (run_task_impl(task) == RetCode_FAILED)
        {
            return RetCode_FAILED;
        }
    }

    return RetCode_OK;
}

inline RetCode Registry::run_task(std::string_view name)
{
    Task* t = find(name);
    if (!t)
    {
        std::cerr << "Task '" << name << "' not found\n";
        return RetCode_FAILED;
    }
    return run_task_impl(t);
}

inline void run(std::string_view name)
{
    Registry::instance().run_task(name);
}

inline void clean() 
{
    Registry::instance().clean();
}

inline void list_tasks() 
{
    Registry::instance().list_tasks();
}

inline void register_task(
    std::string_view name,
    const std::vector<std::string_view>& deps,
    std::function<void(Task*)> action)
{
    Registry::instance().register_task(name, deps, action);
}

} // namespace

/* ========== MACROS ========== */

// TASK macro: works at global, function, or loop scope
// Each task creates a static variable that registers on first execution
// This allows tasks to be declared inside functions or loops
#define TASK(name, ...) \
    static constexpr std::string_view name = #name; \
    void _task_impl_##name(buildtools::Task*); \
    static auto _register_##name = []() { \
        buildtools::Registry::instance().register_task(#name, {__VA_ARGS__}, _task_impl_##name); \
        return 0; \
    }(); \
    void _task_impl_##name(buildtools::Task* task)


#ifdef CPPAKE_MAIN
int main(int argc, char* argv[])
{
    // TODO: handle more than 1 task to run
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <task>\n\n";
        buildtools::list_tasks();
        std::cerr << "\nExample:\n";
        std::cerr << "  " << argv[0] << " build    # Build with dependencies\n";
        std::cerr << "  " << argv[0] << " test     # Test after building\n";
        std::cerr << "  " << argv[0] << " rebuild  # Clean and rebuild\n";
        return 1;
    }

    buildtools::run(argv[1]);
    return 0;
}
#endif // CPPAKE_MAIN
