//----------------------------------------------------------------------
// CONFIG
//----------------------------------------------------------------------
#define COMPILER     "clang++ --std=c++20"
#define BUILD_DIR    "build"
#define NOW_PROGRAM  "app"
#include "now.hpp"
//----------------------------------------------------------------------
// TASKS
//----------------------------------------------------------------------

void build_object(const char* src, const char* obj)
{
    now::StringBuilder sb;

    sb.append(COMPILER);
    sb.append("-Wno-braced-scalar-init"); // warning: braces around scalar initializer [-Wbraced-scalar-init]
    sb.append("-c");
    sb.append(src);
    sb.append("-o");
    sb.append(obj);

    now::sh( sb.to_command().c_str() );
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

    now::sh( sb.to_command().c_str() );
}

int main(int argc, char* argv[])
{  
    TASK(init)
    {
        LOG("Initializing..\n");
        now::mkdir_p( BUILD_DIR );
    };

    TASK(src, {init})
    {
        LOG("Prepare sources...\n");
    };

    TASK(objects, {init, src})
    {
        LOG("Compiling objects...\n");
        build_object( "app.cpp", BUILD_DIR "/app.o");
    };

    TASK(app, {objects})
    {
        LOG("Linking executable...\n");
        link(app, {BUILD_DIR "/app.o"});
    };

    TASK(build, {app})
    {
        LOG("Build complete: app\n");
    };

    TASK(run, {build})
    {
        LOG("Running app...\n");
        now::sh("./app");
    };

    TASK(clean)
    {
        LOG("Removing build artifacts...\n");
        now::sh("rm -rf " BUILD_DIR);
    };

    TASK(rebuild, {clean, build})
    {
        LOG("Rebuild complete!\n");
    };

    TASK(all, {build, run})
    {
        LOG("Full build & test done!\n");
    };

    return now::main(argc, argv);
}