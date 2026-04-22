#pragma once

namespace now
{
    template<typename T>
    struct Array
    {
        static constexpr size_t invalid_index = (size_t)-1;

        size_t      size      = 0;
        void*       data      = nullptr;
        Allocator*  allocator = nullptr;
        //size_t      capacity    = 0;

        Array()
        : size(0)
        , data(nullptr)
        //, capacity(0)
        {
            allocator = default_allocator();
        }

        Array(Allocator* _allocator)
        : size(0)
        , data(nullptr)
        , allocator(_allocator)
        //, capacity(0)
        {
            assert(_allocator != nullptr);
        }

        Array(std::initializer_list<T> list)
        : size(0)
        , data(nullptr)
        //, capacity(0)
        {
            allocator = default_allocator();
            this->resize(list.size());
            for(size_t i = 0; i < size; i++)
                at(i) = *(list.begin()+i);
        }

        void release()
        {
            assert(allocator != nullptr);
            
            // for( size_t i = 0; i < size; i++)
            //     at(i).~T();

            allocator->release(data);
            data     = nullptr;
            size     = 0;
            //capacity = 0;
        }

        void resize(size_t new_size)
        {
            //
            // TODO: exponential capacity grow.
            //       currently an allocation is done each time we resize!!!
            //

            assert(new_size >= size);
            assert(allocator != nullptr);

            // ensure has capacity
            if( new_size > size )
            {
                if( data == nullptr )
                {
                    assert(allocator != nullptr);
                    data = reinterpret_cast<T*>(allocator->allocate( new_size * sizeof(T)));
                    assert(data!=nullptr);
                    size = new_size;
                    
                    for( size_t i=0; i < new_size; i++)
                        new (&at(i)) T(); // construct in-place
                }
                else
                {
                    void*  old_data = data;
                    size_t old_size = size;

                    assert(allocator != nullptr);
                    data = reinterpret_cast<T*>(allocator->reallocate(old_data, new_size * sizeof(T)));
                    assert(data!=nullptr);
                    size = new_size;

                    std::memcpy((void*)data, old_data, old_size);
                    
                    for( size_t i=old_size; i < new_size; i++)
                        new (&at(i)) T(); // construct in-place
                }
            }
            
        }

        void append(const T& str)
        {
            size_t index = size;
            resize( index + 1 );
            *(((T*)data) + index) = str;
        }

        void concat(const Array<T>& other)
        {
            size_t old_size = size;
            resize(size + other.size);
            for (size_t i = 0; i < other.size; ++i )
            {
                this->at(old_size+i) = other.at(i);
            }
        }

        const T& at(size_t pos) const
        { assert(pos < size && "out of bounds"); return *(((T*)data) + pos); }

        T& at(size_t pos)
        { assert(pos < size && "out of bounds"); return *(((T*)data) + pos); }
    };
}