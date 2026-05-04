#pragma once
#include "Allocator.hpp"
#include <cstring>

namespace now 
{
    struct String
    {
        static constexpr size_t npos = (size_t)-1;

        size_t size = {0};
        char*  data = {nullptr};
        Allocator* allocator = nullptr;

        String()
        {
            allocator = heap_allocator();
        }

        String(Allocator* _allocator)
        : allocator(_allocator)
        {
            assert(_allocator != nullptr);
        }

        String(const char* str)
        : size(std::strlen(str))
        , data(const_cast<char*>(str))
        , allocator(nullptr)
        {}

        String(size_t _size, char* _data)
        : size(_size)
        , data(_data)
        {
            allocator = heap_allocator();
        }

// #define NOW_STD_COMPATIBILITY
// #ifdef NOW_STD_COMPATIBILITY
//         explicit String(const std::string& str)
//         : size(str.size())
//         , data(const_cast<char*>(str.data()))
//         {}
// #endif

        ~String()
        {

        }

        void init(size_t _size)
        {
            assert(data == nullptr);
            assert(allocator != nullptr);
            size = _size;
            size_t safe_size = _size + 1; // for '\0'
            data = reinterpret_cast<char*>( allocator->allocate( safe_size ) );
            data[safe_size-1] = 0;
        }

        void release()
        {
            assert( allocator != nullptr );
            allocator->release(data);
            data = nullptr;
        }

        char operator[](size_t pos) const
        { assert(pos < size && "out of bounds"); return data[pos]; }

        size_t rfind(char c) const
        {
            size_t cursor = size-1;
            while ( cursor != npos && data[cursor] != c)
            {
                --cursor;
            }

            return cursor;
        }

        String lsplit(size_t index) const
        {
            assert(index < size && "Out of bounds");
            return String{ index, data };
        }

        String rsplit(size_t index) const
        {
            assert(index < size && "Out of bounds");
            return String{ size - index, data + index };
        }

        String basename()
        {
            size_t last_slash = rfind('\\');
            if ( last_slash == npos )
                return *this;
            return rsplit(last_slash+1);
        }

        String stem() const
        {
            size_t index = rfind('.');
            if( index == npos )
                return *this;
            return lsplit(index);
        }

        const char* cstr() const
        {
            return data;
        }

        static String copy(size_t source_size, char* source_data, Allocator* copy_allocator = default_allocator() )
        { return String::copy(String{source_size, source_data}, copy_allocator); }

        static String copy(const String source, Allocator* copy_allocator = default_allocator())
        {
            String result{copy_allocator};
            result.init(source.size);
            std::memcpy(result.data, source.data, source.size);
            assert(source.size == result.size);
            assert(source.data != result.data);
            return result;
        }

        static int equals(const String& a, const String& b)
        {
            return a.size == b.size && strncmp(a.data, b.data, a.size) == 0;
        }
    };
}            