
#define BUILD_DIR    "build"
#define COMPILER     "clang++"
#define CXXFLAGS     "--std=c++20 -Wno-braced-scalar-init -Wno-braced-scalar-init -DNOW_DEBUG -D_CRT_SECURE_NO_WARNINGS" // warning: braces around scalar initializer [-Wbraced-scalar-init]
#define NOW_IMPLEMENTATION
#define NOW_DEBUG_MEMORY
#define NOW_VERBOSE
#define NOW_ENABLE_TESTS
#include "now/now.hpp"

int main(int argc, char** argv)
{
    // do not put code before this line, it should always be able to rebuild itself
    now::rebuild_it_self_if_needed("task", "task.cpp");

    FILETASK( BUILD_DIR "/main.o", "main.cpp")
    {
        if ( !now::exists(BUILD_DIR))
        {
            now::mkdir_p( BUILD_DIR );
        }

        for(size_t i = 0; i < task->deps.size; ++i)
        {
            now::compile_object( task->deps.at(i) );
        }
    };

    static now::String binary = now::normalize_binary_path( BUILD_DIR "/app.exe" );

    static now::Array<now::String> objects = {
        BUILD_DIR "/main.o"
    };
    
    FILETASK(binary, objects )
    {
        now::link( binary, objects);
    };

    TASK(build, { binary })
    {
    };

    TASK(run, {build})
    {
        now::system(binary);
    };

    TASK(clean)
    {
        now::system("rm -rf " BUILD_DIR);
    };

    TASK(clobber, {clean})
    {
        now::remove(binary);
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