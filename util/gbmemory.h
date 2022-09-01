#pragma once

#include <memory>
#include <functional>
#include <vector>   
#include <new>

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    template <class T, auto Alignment>
    struct aligned_allocator
    {
        using value_type = T;
        [[nodiscard]] constexpr T* allocate(std::size_t n)
        {
            return static_cast<T*>(
                ::operator new (sizeof(T) * n, std::align_val_t{ Alignment }));
        }

        constexpr void deallocate(T* p, std::size_t n)
        {
            ::operator delete (p, n, std::align_val_t{ Alignment });
        }

        constexpr auto operator==(const aligned_allocator&) const { return true; }

        template <typename U> struct rebind
        {
            using other = aligned_allocator<U, Alignment>;
        };
    };

    //-------------------------------------------------------------------------
    template <class T, auto Alignment>
    using aligned_vector = std::vector<T, aligned_allocator<T, Alignment>>;

    //-------------------------------------------------------------------------
    // make unique_ptr of an array, default initialized, using Allocator
    //-------------------------------------------------------------------------
    template <class T, class Allocator> 
    auto make_unique_array(std::size_t size)
    {
        return std::unique_ptr<T[], std::function<void(T*)>>(
            [=] {
                auto p = Allocator{}.allocate(size);
                std::uninitialized_default_construct_n(p, size);
                return p;
            }(),
                [=](T* p) {
                std::destroy_n(p, size);
                Allocator{}.deallocate(p, size);
            });
    }

    //-------------------------------------------------------------------------
    // make unique_ptr of an alligned array, default initialized
    //-------------------------------------------------------------------------
    template <class T, std::size_t Alignment>
    auto make_unique_array(std::size_t size)
    {
        return make_unique_array<T, aligned_allocator<T, Alignment>>(size);
    }

    //-------------------------------------------------------------------------
    // unique_ptr of an array of type T with a deleter
    //-------------------------------------------------------------------------
    template <class T>
    using unique_array = std::unique_ptr<T[], std::function<void(T*)>>;

    //-------------------------------------------------------------------------
    template <class T, std::size_t alignment, std::size_t size>
    struct aligned_array
    {
        static_assert(alignment% alignof(T) == 0);
        alignas(alignment) T data[size];
        constexpr T& operator[](std::size_t n) { return data[n]; }
        constexpr const T& operator[](std::size_t n) const { return data[n]; }
    };

}
