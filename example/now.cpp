//----------------------------------------------------------------------
// CONFIG
//----------------------------------------------------------------------
#define COMPILER     "clang++"
#define CXXFLAGS     "-O0 -g --std=c++20 -Wno-braced-scalar-init" // warning: braces around scalar initializer [-Wbraced-scalar-init]
#define BUILD_DIR    "build"
#define NOW_PROGRAM  "app"
#include "../now.hpp"
#include <format>
//----------------------------------------------------------------------
// TASKS
//----------------------------------------------------------------------

void build_object(now::String src)
{
    now::StringBuilder sb;

    sb.append(COMPILER);
    sb.append(CXXFLAGS); 
    sb.append("-c");
    sb.append(src);
    sb.append("-o");

    now::StringBuilder obj;
    obj.append(BUILD_DIR);
    obj.append(src.stem());
    obj.append(".o");

    sb.append(obj.join_to_temp_cstr());

    now::system( sb.join_to_temp_cmd() );
}

void link(const char* binary, const std::vector<const char*>& objects)
{
    now::StringBuilder sb;

    sb.append(COMPILER);

    for (auto each_obj : objects)
    {
        sb.append(each_obj);
    }

    sb.append("-o");
    sb.append(binary);

    now::system( sb.join_to_temp_cmd() );
}

NOW_STATIC_INITIALIZER

    FILETASK( BUILD_DIR "/main.o", "main.cpp")
    {
        if ( !now::file_exists(BUILD_DIR))
        {
            now::mkdir_p( BUILD_DIR );
        }

        for(const char* src : task->deps)
        {
            build_object( src );
        }
    };

    FILETASK("app.exe", {BUILD_DIR "/main.o"})
    {
        link(task->name, task->deps);
    };

    TASK(build, {"app.exe"})
    {
    };

    TASK(run, {build})
    {
        now::system("app.exe");
    };

    TASK(clean)
    {
        now::system("rm -rf " BUILD_DIR);
    };

    TASK(clobber, {clean})
    {
        now::system("rm app.exe");
    };

    TASK(rebuild, {clean, build})
    {
    };

    TASK(all, {build, run})
    {
    };

NOW_STATIC_INITIALIZER_END

int main(int argc, char* argv[])
{  
    return now::main(argc, argv);
}