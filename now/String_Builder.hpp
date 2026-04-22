#pragma once
#include <cstring>
#include "String.hpp"
#include "Array.hpp"
#include "Allocator.hpp"
#include "Logging.hpp"

namespace now
{
    struct StringBuilder
    {
        Array<String> data;

        StringBuilder()
        : data()
        {
            data.allocator = default_allocator();
        }

        StringBuilder(Allocator* allocator)
        : data()
        {
            data.allocator = allocator;
        }

        StringBuilder&  append(const char* str);
        StringBuilder&  append(String str);
        template<typename T>  
        StringBuilder&  append(const Array<T>& arr);              
        String          build_string(String separator = "", Allocator* allocator = default_allocator() );
    };

    static String join(const Array<String>& arr, String separator, Allocator* string_allocator = default_allocator() );
    static String join(const Array<String>& arr, Allocator* string_allocator = default_allocator() )
    { return join(arr, "", string_allocator ); }
}

now::StringBuilder& now::StringBuilder::append(const char* str)
{
    append(String{str});
    return *this;
}

now::StringBuilder& now::StringBuilder::append(String str)
{
    data.append(str);
    return *this;
}

template<typename T>
now::StringBuilder& now::StringBuilder::append(const Array<T>& arr)
{
    for(size_t i = 0; i < arr.size; ++i)
    {
        append( arr.at(i) );
    }
    return *this;
}

now::String now::StringBuilder::build_string(String separator, Allocator* allocator)
{
    return join(this->data, separator, allocator);
}

now::String now::join(const Array<String>& arr, String separator, Allocator* string_allocator )
{
    assert(string_allocator != nullptr);

    // init a string to store each data with a separator
    size_t temp_size = 0;
    for( size_t i = 0; i < arr.size; i++)
        temp_size += arr.at(i).size;
    if( separator.size > 0 && arr.size > 1)
        temp_size += separator.size * (arr.size-1); // 1 separator after each, except last

    String result{string_allocator};
    result.init(temp_size);
    
    assert(result.data != nullptr);
    char* cursor = result.data;
    for(size_t i = 0; i < arr.size; ++i)
    {
        if ( i && separator.size )
        {
            std::memcpy(cursor, separator.data, separator.size);
            cursor += separator.size;
            LOG_DEBUG("%s\n", result.data);
        }
        char* dst = arr.at(i).data;
        assert(dst != nullptr);
        std::memcpy(cursor, dst, arr.at(i).size);
        cursor += arr.at(i).size;
        LOG_DEBUG("%s\n", result.data);
    }
    LOG_DEBUG("%s\n", result.data);

    return result;
}