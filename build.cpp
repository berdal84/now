#define CPPAKE_MAIN // will include default main()
#include "lib/build-tools.hpp"

#define COMPILER    "clang++"

#ifdef RELEASE
#define BUILD_DIR   "build-release"
#else
#define BUILD_DIR   "build-debug"
#endif

/* ========== TASK DEFINITIONS ========== */

// Task with no dependencies
TASK(init) {
    LOG("Initializing..\n");
    MKDIR_P( BUILD_DIR );
}

// Task with single dependency
TASK(src, {init}) {
    LOG("Prepare sources...\n");
}

// Task with multiple dependencies
TASK(objects, {init, src}) {
    LOG("Compiling objects...\n");
    SH( COMPILER " -c -o " BUILD_DIR "/main.o main.cpp");
}

// Diamond dependency pattern
TASK(link, {objects}) {
    LOG("Linking executable...\n");
    SH( COMPILER  " -o " BUILD_DIR "/app " BUILD_DIR "/main.o");
}

// Final artifact
TASK(build, {link}) {
    LOG("Build complete: " BUILD_DIR "/app\n");
}

// Testing
TASK(run, {build}) {
    LOG("Running tests...\n");
    SH("./" BUILD_DIR "/app");
}

// Cleanup
TASK(clean) {
    LOG("Removing build artifacts...\n");
    SH("rm -rf " BUILD_DIR);
}

// Compound task: clean + build
TASK(rebuild, {clean, build}) {
    LOG("Rebuild complete!\n");
}

// Full pipeline
TASK(all, {build, run}) {
    LOG("Full build & test done!\n");
}
