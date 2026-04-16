
#define BINARY       "app.exe"
#define BUILD_DIR    "build"
#define COMPILER     "clang++"
#define CXXFLAGS     "--std=c++20 -Wno-braced-scalar-init" // warning: braces around scalar initializer [-Wbraced-scalar-init]
#define NOW_IMPLEMENTATION
#define NOW_DEBUG_MEMORY
#define NOW_VERBOSE
#define NOW_ALWAYS_REBUILD
#define NOW_ENABLE_TESTS
#include "now/now.hpp"
#include <format>

int main(int argc, char** argv)
{
    NOW_INITIALIZE();

    // do not put code before this line, it should always be able to rebuild itself
    now::rebuild_it_self_if_needed("task.exe", "task.cpp");

    FILETASK( BUILD_DIR "/main.o", "main.cpp")
    {
        if ( !now::exists(BUILD_DIR))
        {
            now::mkdir_p( BUILD_DIR );
        }

        for(size_t i; i < task->deps.size; ++i)
        {
            now::compile_object( task->deps.at(i) );
        }
    };

    static now::Array<now::String> objects = {
        BUILD_DIR "/main.o"
    };
    
    FILETASK(BINARY, objects )
    {
        now::link( BINARY, objects);
    };

    TASK(build, { BINARY })
    {
    };

    TASK(run, {build})
    {
        now::system("./" BINARY);
    };

    TASK(clean)
    {
        now::system("rm -rf " BUILD_DIR);
    };

    TASK(clobber, {clean})
    {
        now::system("rm " BINARY);
    };

    TASK(rebuild, {clean, build})
    {
    };

    TASK(all, {build, run})
    {
    };

    now::init();
    now::parse_args(argc, argv);

    return 0;
}