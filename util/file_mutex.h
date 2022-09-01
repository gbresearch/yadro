#pragma once

#include <thread>
#include <chrono>
#include "gberror.h"

#ifdef POSIX
namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // spin and wait class
    //-------------------------------------------------------------------------
    struct spin_wait
    {
        void yield()
        {
            if (spin_count < (nop_pause_limit >> 2)) { /*nop*/ }
            // Intel pause: https://tinyurl.com/yb3lh2ph
            else if (spin_count < nop_pause_limit) { __asm__ __volatile__("rep; nop" : : : "memory"); }
            else { std::this_thread::yield(); }
            ++spin_count;
        }

    private:
        unsigned long long spin_count{ num_cores > 1u ? 0u : nop_pause_limit };
        static constexpr unsigned nop_pause_limit = 32u;
        static inline const auto num_cores = std::thread::hardware_concurrency();
    };

    //-------------------------------------------------------------------------
    // global mutex for threads and processes
    //-------------------------------------------------------------------------
    struct file_mutex
    {
        // constructors
        file_mutex() = default;

        file_mutex(const char* file_name);

        file_mutex(const file_mutex& other) = delete;

        ~file_mutex();

        // lock operations
        void lock();

        bool try_lock();

        void unlock();

        // timed lock operations
        template< class Rep, class Period >
        bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration)
        {
            return try_lock_until(std::chrono::high_resolution_clock::now() + timeout_duration);
        }

        template< class Clock, class Duration >
        bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
        {
            spin_wait spinner;
            while (std::chrono::high_resolution_clock::now() < timeout_time)
            {
                if (try_lock())
                    return true;
                spinner.yield();
            }
            return false;
        }

        // for shared mutex
        void lock_shared();

        bool try_lock_shared();

        void unlock_shared();

        template< class Rep, class Period >
        bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration)
        {
            return try_lock_shared_until(std::chrono::high_resolution_clock::now() + timeout_duration);
        }

        template< class Clock, class Duration >
        bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
        {
            spin_wait spinner;
            while (std::chrono::high_resolution_clock::now() < timeout_time)
            {
                if (try_lock_shared())
                    return true;
                spinner.yield();
            }
            return false;
        }

        // writing additional binary data in the lock file
        template<class T>
        void write(const T& t) const
        {
            static_assert(std::is_trivial_v<T>);
            if (_handle != invalid_handle)
            {
                auto err = ::lseek(_handle, 0, SEEK_SET);
                gbassert(err != -1);
                err = ::write(_handle, static_cast<const void*>(std::addressof(t)), sizeof(T));
                gbassert(err != -1);
            }
            else
                gbassert(!"write failed: invalid handle");
        }

        template<class T>
        T read() const
        {
            static_assert(std::is_trivial_v<T>);
            T result;
            gbassert(_handle != invalid_handle);
            auto err = ::lseek(_handle, 0, SEEK_SET);
            gbassert(err != -1);
            err = ::read(_handle, static_cast<void*>(std::addressof(result)), sizeof(T));
            gbassert(err != -1);

            return result;
        }

        bool is_empty() const
        {
            auto size = ::lseek(_handle, 0, SEEK_END);
            gbassert(size != -1);
            return size == 0;
        }

    private:
        using file_handle_t = int;
        static constexpr file_handle_t invalid_handle = -1;
        file_handle_t _handle = invalid_handle;
        unsigned long long unique_tid = syscall(SYS_gettid);
    };

}
#endif
