#pragma once

#include <cstdarg>
#include <cstdio>

#define LOG(FMT, ...) now::log_message(FMT, __VA_ARGS__ )

#ifdef NOW_VERBOSE
#   define LOG_DEBUG(FMT, ...) now::log_message("[debug] " FMT, __VA_ARGS__ )
#else
#   define LOG_DEBUG(FMT, ...)
#endif

namespace now
{
    void log_message(const char *format, ...);
}

void now::log_message(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

#ifdef NOW_DEBUG
    // In case of crash, we want to see the output immediately
    _flushall();
#endif

}