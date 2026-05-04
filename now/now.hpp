#pragma once

#include <vector>
#include <string_view>
#include <unordered_map>
#include <filesystem>

#include "src/Allocator.hpp"
#include "src/Array.hpp"
#include "src/Logging.hpp"
#include "src/String_Builder.hpp"

namespace now
{
    struct Task;
    struct State;
    enum class Task_Type;
    enum class Code;

    // Common  ----------------------------------------------------------------------------------------
    static void         init();
    static int          run(const String& binary, const Array<String>& args= {}, bool fatal = true);
    static int          system(const String& command, const Array<String>& args= {}, bool fatal = true);
    static int          parse_args(int argc, char* argv[]);

#ifdef NOW_ENABLE_TESTS
    static void         run_tests();
#endif

    // Compilation -------------------------------------------------------------------------------------
    static void         rebuild_it_self_if_needed(String binary, String source);
    static void         compile_object(now::String src);
    static void         link(String binary, Array<String>& objects);

    // Filesystem -------------------------------------------------------------------------------------

    static void         remove(const String& path);
    static void         rename(const String& src, const String& dst);
    static int          mkdir_p(const String& path);
    static bool         exists(const String& path);
    static String       normalize_binary_path(const String& binary);

    // Tasks --- --------------------------------------------------------------------------------------

#define TASK( TASK_NAME, ...) \
    static const char* TASK_NAME = #TASK_NAME; \
    now::new_task( \
        now::get_state(), \
        now::Task_Type::REGULAR,\
        #TASK_NAME, \
        {__VA_ARGS__} \
    )->action = [](const now::Task* task) -> void

#define FILETASK( FILE_NAME, ...) \
    now::new_task( \
        now::get_state(), \
        now::Task_Type::FILE,\
        FILE_NAME, \
        {__VA_ARGS__} \
    )->action = [](const now::Task* task) -> void

    static State&       get_state();
    static void         reset_state(State& state);
    static Task*        new_task( State& state, Task_Type type, String name, Array<String> deps);
    static const Task*  find_task(const State& state, const String& name);
    static Code         invoke_task(const State& state, const Task* task);
    static void         print_tasks(State& state);
    static void         print_help(State& state);
    static Code         invoke_tasks_sequentially(const State& state, const std::vector<Task*>& tasks);


    enum class Code {
        FAILED = 0,
        OK,
        OK_SKIPPED
    };

    enum class Task_Type {
        NONE = 0,
        REGULAR,
        FILE
    };

    struct Task
    {
        using Action = void(*)(const Task*);
        static void null_action(const Task*) {}

        mutable bool        done = false;
        Task_Type           type = Task_Type::NONE;
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
}

#ifdef NOW_IMPLEMENTATION

now::String now::normalize_binary_path(const String& binary)
{
    assert(binary.size && "binary is empty!");
    assert(binary[0] != '/' && "absolute path not handled yet");

    #if __unix__ or __DARWIN__
        return join({"./", binary.stem()}, "", temp_allocator() );
    #else
        return join({"./", binary.stem(), ".exe"}, "", temp_allocator() );
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
    return now::system( "mkdir", Array<String>{ "-p", path } );
#else
    return now::system( "mkdir", Array<String>{ path } );
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
        Task_Type      type,
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
        return Code::OK_SKIPPED;
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
            return Code::FAILED;
        }
        else if( invoke_task(state, dep_task) == Code::FAILED )
        {
            return Code::FAILED;
        }
    }
    
    
    if (task->action != nullptr)
    {
        task->action(task);
    }
    
    task->done = true;
    LOG_DEBUG("-- Invoke task %s DONE\n", task->name.cstr());

    return Code::OK;
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

now::Code now::invoke_tasks_sequentially(const State& state, const std::vector<Task*>& tasks)
{
    for (Task* task : tasks)
    {       
        if ( now::invoke_task(state, task) == Code::FAILED)
        {
            return Code::FAILED;
        }
    }

    return Code::OK;
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

        if( invoke_task(state, task) == Code::FAILED )
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
    now::system( COMPILER, { CXXFLAGS, "-c", src.cstr(), "-o", join(obj, temp_allocator() ).cstr() } );

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
         now::rename("task.pdb", "task.pdb.old");

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
