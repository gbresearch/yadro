#pragma once
#include <functional>
#include <mutex>
#include <concepts>
#include <span>
#include <compare>

// miscellaneous utilities

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // resource locked with mutex from construction to destruction
    //-------------------------------------------------------------------------
    template<class T, class Mutex = std::mutex>
    struct locked_resource
    {
        // construct with owning mutex
        template<class...Args>
        constexpr explicit locked_resource(Args&&... args)
        {
            new(&t) T(std::forward<Args>(args)...);
        }

        // construct with mutex reference
        template<class...Args, class MM = Mutex, std::enable_if_t<std::is_reference_v<MM>, int> = 0>
        constexpr explicit locked_resource(MM& m, Args&&... args) : mtx(m)
        {
            std::scoped_lock _(mtx);
            new(&t) T(std::forward<Args>(args)...);
        }

        ~locked_resource()
        {
            t.~T();
        }

        template<class Function, class...Args>
        constexpr auto visit(Function&& f, Args&&...args)
        {
            return std::scoped_lock(mtx),
                std::invoke(std::forward<Function>(f), t, std::forward<Args>(args)...);
        }

        template<class Function, class...Args>
        constexpr auto visit(Function&& f, Args&&...args) const
        {
            return std::scoped_lock(mtx),
                std::invoke(std::forward<Function>(f), t, std::forward<Args>(args)...);
        }

        constexpr explicit operator bool() const { return std::scoped_lock(mtx), (t ? true : false); }

    private:
        Mutex mtx;
        union { T t; };
    };

    inline auto locked_call(auto fun, auto& ... mtx) requires (std::invocable<decltype(fun)>)
    {
        std::scoped_lock lock(mtx ...);
        return std::invoke(fun);
    }

    //-------------------------------------------------------------------------
    template<class OnExit>
    struct raii
    {
        raii(auto on_entry, OnExit on_exit) requires (std::invocable<decltype(on_entry)>)
            : on_exit(on_exit) {
            std::invoke(on_entry);
        }
        raii(OnExit on_exit) : on_exit(on_exit) { }
        ~raii() { std::invoke(on_exit); }
    private:
        OnExit on_exit;
    };

    template<class OnExit, class OnEntry>
    raii(OnEntry, OnExit)->raii<OnExit>;

    //---------------------
    // span 3-way comparison
    template<class T1, std::size_t Extent1, class T2, std::size_t Extent2>
    auto compare(const std::span<T1, Extent1>& s1, const std::span<T2, Extent2>& s2) requires(std::three_way_comparable_with<T1, T2>)
    {
        using ordering_t = std::compare_three_way_result_t<T1, T2>;
        if (s1.size() < s2.size())
            return ordering_t::less;
        else if (s1.size() > s2.size())
            return ordering_t::greater;
        else for (std::size_t i = 0; i < s1.size(); ++i)
        {
            auto cmp = s1[i] <=> s2[i];
            if (std::is_neq(cmp))
                return cmp;
        }
        return ordering_t::equivalent;
    }

    //---------------------
    template<class CharT, class Traits>
    auto compare(const CharT* s1, const CharT* s2, std::size_t size1, std::size_t size2) {
        auto comp = Traits::compare(s1, s2, std::min(size1, size2));
        return comp < 0 ? std::weak_ordering::less
            : comp > 0 ? std::weak_ordering::greater
            : size1 < size2 ? std::weak_ordering::less
            : size1 > size2 ? std::weak_ordering::greater
            : std::weak_ordering::equivalent;
    }

    //-------------------------------------------------------------------------
    constexpr auto strings_equal(const char* s1, const char* s2) {
        return std::char_traits<char>::length(s1) ==
            std::char_traits<char>::length(s2) &&
            std::char_traits<char>::compare(
                s1, s2, std::char_traits<char>::length(s1)) == 0;
    }

    //-------------------------------------------------------------------------
    const auto to_string = [](auto&&... args)
    {
        std::stringstream ss;
        if constexpr (sizeof ...(args) != 0) // avoid -Werror=unused-value
            (ss << ... << args);
        return ss.str();
    };

}
