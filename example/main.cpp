//----------------------------------------------------------------------
// CONFIG
//----------------------------------------------------------------------
#define COMPILER     "clang++"
#define CXXFLAGS     "-O0 -g --std=c++20 -Wno-braced-scalar-init" // warning: braces around scalar initializer [-Wbraced-scalar-init]
#define BUILD_DIR    "build"
#include "../now.hpp"
#include <format>
//----------------------------------------------------------------------
// TASKS
//----------------------------------------------------------------------

NOW_STATIC_INITIALIZER

    FILETASK( BUILD_DIR "/main.o", "main.cpp")
    {
        if ( !now::exists(BUILD_DIR))
        {
            now::mkdir_p( BUILD_DIR );
        }

        for(const char* src : task->deps)
        {
            now::compile_object( src );
        }
    };

    static const char* binary = "app.exe";
    static const std::vector<const char*> objects{
        BUILD_DIR "/main.o"
    };

    FILETASK(binary, objects )
    {
        now::link(binary, objects);
    };

    TASK(build, {"app.exe"})
    {
    };

    TASK(run, {build})
    {
        printf("-- Welcome to this Example --\n");
        printf("Good Bye!");
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