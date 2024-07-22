//-----------------------------------------------------------------------------
//  Copyright (C) 2024, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//
//  Permission is hereby granted, free of charge, to any person or organization
//  obtaining a copy of the software and accompanying documentation covered by
//  this license (the "Software") to use, reproduce, display, distribute,
//  execute, and transmit the Software, and to prepare derivative works of the
//  Software, and to permit third-parties to whom the Software is furnished to
//  do so, all subject to the following:
//
//  The copyright notices in the Software and this entire statement, including
//  the above license grant, this restriction and the following disclaimer,
//  must be included in all copies of the Software, in whole or in part, and
//  all derivative works of the Software, unless such copies or derivative
//  works are solely in the form of machine-executable object code generated by
//  a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#pragma once
#include "gbwin.h"
#include "tuple_functions.h"
#include "gblog.h"
#include "misc.h"
#include "time_util.h"
#include "string_util.h"
#include "../archive/archive.h"
#include "../async/threadpool.h"

#include <string>
#include <variant>
#include <tuple>
#include <memory>
#include <atomic>
#include <type_traits>

#ifdef GBWINDOWS

namespace gb::yadro::util
{
    struct owinpipe_stream;
    struct iwinpipe_stream;
    using owinpipe_archive = gb::yadro::archive::archive<owinpipe_stream, gb::yadro::archive::archive_format_t::custom>;
    using iwinpipe_archive = gb::yadro::archive::archive<iwinpipe_stream, gb::yadro::archive::archive_format_t::custom>;

    // single-instance server
    struct winpipe_server_t;
    struct winpipe_client_t;

    // multi-instance server
    template<class ...Fn>
    void start_server(const std::wstring& pipename, std::shared_ptr<util::logger> log, Fn&&... fn);
    void shutdown_server(const std::wstring& pipename, unsigned attempts, auto&&...log_args);

    // server function concept
    template<class T, class Functions>
    concept server_function_c = std::convertible_to< T, std::tuple<std::string, Functions>>;

    // constants
    inline constexpr auto pipe_chunk_size = 1024;
    inline constexpr unsigned server_disconnect = -1;
    inline constexpr unsigned server_shutdown = -2;

    //----------------------------------------------------------------------------------------------
    struct owinpipe_stream
    {
        using char_type = char;
        explicit owinpipe_stream(HANDLE pipe) : _pipe(pipe) {}

        void write(const char_type* c, std::streamsize size)
        {
            // write in chunks
            char next_chunk[] = "next\0";

            for (std::streamsize sent_bytes = 0; sent_bytes < size;)
            {
                // read request for the next chunk
                if (DWORD bytes_read{};
                    not ReadFile(_pipe, next_chunk, (DWORD)sizeof(next_chunk), &bytes_read, nullptr)
                    || bytes_read != (DWORD)sizeof(next_chunk))
                    throw util::exception_t("owinpipe_stream failed to receive next chunk request: ", GetLastError());

                auto bytes_to_send = size - sent_bytes > pipe_chunk_size ? pipe_chunk_size : size - sent_bytes;

                if (DWORD bytes_written{};
                    not WriteFile(_pipe, &c[sent_bytes], (DWORD)bytes_to_send, &bytes_written, nullptr)
                    || bytes_written != bytes_to_send)
                    throw util::exception_t("owinpipe_stream failed to write chunk to pipe: ", GetLastError());
                else
                    sent_bytes += bytes_written;
            }
        }

    private:
        HANDLE _pipe = INVALID_HANDLE_VALUE;
    };


    //----------------------------------------------------------------------------------------------
    struct iwinpipe_stream
    {
        using char_type = char;
        explicit iwinpipe_stream(HANDLE pipe) : _pipe(pipe) {}

        void read(char_type* c, std::streamsize size)
        {
            // read in chunks
            char next_chunk[] = "next\0";

            for (std::streamsize received_bytes = 0; received_bytes < size; gbassert(received_bytes <= size))
            {
                // request the next chunk
                if (DWORD bytes_written{};
                    not WriteFile(_pipe, next_chunk, (DWORD)sizeof(next_chunk), &bytes_written, nullptr)
                    || bytes_written != (DWORD)sizeof(next_chunk))
                    throw util::exception_t("iwinpipe_stream failed to request next chunk: ", GetLastError());

                if (DWORD bytes_read{};
                    not ReadFile(_pipe, &c[received_bytes], pipe_chunk_size, &bytes_read, nullptr) || bytes_read == 0)
                    throw util::exception_t("iwinpipe_stream failed to read chunk from pipe: ", GetLastError());
                else
                    received_bytes += bytes_read;
            }
        }

    private:
        HANDLE _pipe = INVALID_HANDLE_VALUE;
    };

    
    //----------------------------------------------------------------------------------------------
    struct winpipe_base_t
    {
        winpipe_base_t(auto&&... log_args) 
            : _log(std::make_shared<util::logger>(std::forward<decltype(log_args)>(log_args)...))
        {}

        winpipe_base_t(std::shared_ptr<util::logger> log) : _log(log) {}

        winpipe_base_t(winpipe_base_t&& other) noexcept
            : _pipe(other._pipe), _log(std::move(other._log)) { other._pipe = INVALID_HANDLE_VALUE; }

        void set_logger(auto&&...args) 
        {
            _log = std::make_shared<util::logger>(std::forward<decltype(args)>(args)...);
        }

        void set_send_receive_log(bool set) { _log_send_receive = set; }
        void log(auto&&...args) const { if (_log) _log->writeln(util::time_stamp(), ':', _pipe, ':', std::forward<decltype(args)>(args)...); }

        template<class T> requires(archive::is_serializable_v<iwinpipe_archive, std::remove_cvref_t<T>>)
        void receive(T& t) const
        {
            iwinpipe_archive a{ _pipe };
            a(t);
            if(_log_send_receive)
                log("received: ", archive::serialization_size(t), " bytes");
        }

        template<class T> requires(archive::is_serializable_v<iwinpipe_archive, std::remove_cvref_t<T>>)
        T receive() const
        {
            T t;
            receive(t);
            return t;
        }
        
        template<class T> requires(std::is_void_v<T>)
        auto receive() const { return std::tuple{}; }

        template<class T> requires(archive::is_serializable_v<owinpipe_archive, std::remove_cvref_t<T>>)
        void send(const T& t) const
        {
            if (_log_send_receive)
                log("sending: ", archive::serialization_size(t), " bytes");
            owinpipe_archive a{ _pipe };
            a(t);
        }

        auto get_handle() const { return _pipe; }
    
    protected:
        HANDLE _pipe = INVALID_HANDLE_VALUE;
    private:
        std::shared_ptr<util::logger> _log;
        bool _log_send_receive = false;
    };

    //----------------------------------------------------------------------------------------------
    struct winpipe_client_t : winpipe_base_t
    {
        // anonymous client
        winpipe_client_t(const std::wstring& pipename, unsigned connection_attempts, auto&& ...log_args) // Win10, v 1709 "\\\\.\\pipe\\LOCAL\\"
            : winpipe_client_t(pipename, "", connection_attempts, std::forward<decltype(log_args)>(log_args)...)
        {}
        
        // named client
        winpipe_client_t(const std::wstring& pipename, std::string client_name, unsigned connection_attempts, auto&& ...log_args) // Win10, v 1709 "\\\\.\\pipe\\LOCAL\\"
            : winpipe_base_t(std::forward<decltype(log_args)>(log_args)...), _client_name(std::move(client_name))
        {
            using namespace std::chrono_literals;
            auto attempt = 0u;
            for(; _pipe == INVALID_HANDLE_VALUE && attempt < connection_attempts; ++attempt)
            {
                if (attempt != 0)
                    std::this_thread::sleep_for(10ms);

                _pipe = CreateFile(
                        pipename.c_str(),
                        GENERIC_READ |  // read and write access 
                        GENERIC_WRITE,
                        0,              // no sharing 
                        nullptr,        // default security attributes
                        OPEN_EXISTING,  // opens existing pipe 
                        0,              // default attributes 
                        nullptr);       // no template file 
            }

            if(_pipe == INVALID_HANDLE_VALUE)
            {
                std::string str_name(pipename.size(), 0);
                std::transform(pipename.begin(), pipename.end(), str_name.begin(), [](auto wc) { return static_cast<char>(wc); });
                auto error_string = util::to_string(_client_name, ": failed to open pipe: ", str_name, ": ", GetLastError());
                log(error_string);
                throw util::exception_t(error_string);
            }
            log(_client_name, ": opened pipe after ", attempt, " attempts");
        }
        ~winpipe_client_t()
        {
            log(_client_name, ": destructor");
            disconnect();
        }

        template<class T>
        [[nodiscard]]
        auto request(const std::string& name, auto&& ...params) const
        {
            log(_client_name, ": sending request for function: ", name);
            send(0u);
            send(name);
            if constexpr (sizeof ...(params))
                send(std::tuple{ params... });
            return receive<std::expected<T, std::string>>();
        }

        template<class T>
        [[nodiscard]]
        auto request(unsigned fn_id, auto&& ...params) const
        {
            log(_client_name, ": sending request");
            send(fn_id);
            if constexpr(sizeof ...(params))
                send(std::tuple{ params... });
            return receive<std::expected<T, std::string>>();
        }

        template<auto Index, class T>
        [[nodiscard]]
        auto request(auto&& ...params) const
        {
            return request<T>(Index, std::forward<decltype(params)>(params)...);
        }

        void disconnect()
        {
            if (_pipe != INVALID_HANDLE_VALUE)
            {
                send<unsigned>(server_disconnect);
                CloseHandle(_pipe);
                _pipe = INVALID_HANDLE_VALUE;
            }
        }

        void shutdown()
        {
            if (_pipe != INVALID_HANDLE_VALUE)
            {
                send<unsigned>(server_shutdown);
                CloseHandle(_pipe);
                _pipe = INVALID_HANDLE_VALUE;
            }
        }

        auto&& get_name() const { return _client_name; }

    private:
        std::string _client_name;
    };

    //----------------------------------------------------------------------------------------------
    struct winpipe_server_t : winpipe_base_t
    {
        winpipe_server_t(const std::wstring& pipename, auto&& ...log_args);

        winpipe_server_t(winpipe_server_t&& other) : winpipe_base_t(static_cast<winpipe_base_t&&>(other)) {}
        
        ~winpipe_server_t()
        {
            log("server destructor");

            if (_pipe != INVALID_HANDLE_VALUE)
            {
                FlushFileBuffers(_pipe);
                DisconnectNamedPipe(_pipe);
                CloseHandle(_pipe);
            }
        }

        void run(async::threadpool<>&);

        // functions are invoked by index
        template<class ...Fn>
        int run(Fn... functions)
        {
            if (_pipe == INVALID_HANDLE_VALUE)
                throw util::exception_t("can't run unconnected server");

            static_assert(sizeof...(Fn) != 0, "server must have at least one function");
            std::tuple<Fn...> fun_tuple{ functions... };

            for (;;)
            {
                auto fun_index = receive<unsigned>();

                if (fun_index == server_shutdown)
                {
                    log("client requested shutdown");
                    return server_shutdown;
                }

                if (fun_index == server_disconnect)
                {
                    log("client requested disconnect");
                    return 0;
                }

                if (fun_index >= sizeof...(functions))
                {
                    log("error: client requested invalid function: ", fun_index);
                    return 0;
                }

                log("client requested function: ", fun_index);

                std::visit([&](auto&& fn)
                    {
                        using lambda_type = std::remove_cvref_t<decltype(fn)>; // tuple
                        auto params = receive<lambda_pure_args<lambda_type>>();
                        log("received parameters for function: ", fun_index);
                        using sent_t = std::expected< lambda_ret<lambda_type>, std::string>;

                        try
                        {
                            if constexpr (std::is_void_v<lambda_ret<lambda_type>>)
                            {
                                std::apply([&](auto&& ...args)
                                    {
                                        fn(std::move(args)...); // no response required
                                    }, params);
                                send(sent_t{});
                            }
                            else
                                send(sent_t{ std::apply([&](auto&& ...args)
                                {
                                    return fn(std::move(args)...);
                                }, params) });
                            log("sent response from function: ", fun_index);
                        }
                        catch (util::exception_t<>& e)
                        {
                            log("exception: ", e.what());
                            send(sent_t{ std::unexpected{e.what()} });
                        }
                        catch (std::exception& e)
                        {
                            log("exception: ", e.what());
                            send(sent_t{ std::unexpected{e.what()} });
                        }
                        catch (...)
                        {
                            log("unknown exception");
                            send(sent_t{ std::unexpected{std::string("unknown exception")} });
                        }

                    }, util::tuple_to_variant(fun_tuple, fun_index));
            }
        }

        // functions are invoked by name
        template<class ...Fn>
        int run(std::tuple<const char*, Fn> ... functions)
        {
            if (_pipe == INVALID_HANDLE_VALUE)
                throw util::exception_t("can't run unconnected server");

            static_assert(sizeof...(Fn) != 0, "server must have at least one function");
            auto tuple_functions{ std::tuple{functions...} };

            for (;;)
            {
                auto call_id = receive<unsigned>();

                if (call_id == server_shutdown)
                {
                    log("client requested shutdown");
                    return server_shutdown;
                }

                if (call_id == server_disconnect)
                {
                    log("client requested disconnect");
                    return 0;
                }

                auto fun_name = receive<std::string>();
                log("client requested function: ", fun_name);

                try
                {
                    std::visit([&](auto&& fn_tuple)
                        {
                            auto&& fn = std::get<1>(fn_tuple);
                            using lambda_type = std::remove_cvref_t<decltype(fn)>; // tuple
                            auto params = receive<lambda_pure_args<lambda_type>>();
                            log("received parameters for function: ", fun_name);
                            using sent_t = std::expected< lambda_ret<lambda_type>, std::string>;

                            if constexpr (std::is_void_v<lambda_ret<lambda_type>>)
                            {
                                std::apply([&](auto&& ...args)
                                    {
                                        fn(std::move(args)...); // no response required
                                    }, params);
                                send(sent_t{});
                            }
                            else
                                send(sent_t{ std::apply([&](auto&& ...args)
                                {
                                    return fn(std::move(args)...);
                                }, params) });
                            log("sent response from function: ", fun_name);

                        }, util::tuple_to_variant(tuple_functions, [&](auto&& fun_tup) { return std::get<0>(fun_tup) == fun_name; }));
                }
                catch (util::exception_t<>& e)
                {
                    log("exception: ", e.what());
                    send(std::expected<void, std::string>{ std::unexpected{e.what()} });
                }
                catch (std::exception& e)
                {
                    log("exception: ", e.what());
                    send(std::expected<void, std::string>{ std::unexpected{e.what()} });
                }
                catch (...)
                {
                    log("unknown exception");
                    send(std::expected<void, std::string>{ std::unexpected{ "unknown exception" }});
                }
            }
        }
    };

    //----------------------------------------------------------------------------------------------
    inline winpipe_server_t::winpipe_server_t(const std::wstring& pipename, auto&& ...log_args)
        : winpipe_base_t(std::forward<decltype(log_args)>(log_args)...)
    {
        const DWORD buf_size = 4096; // unsigned long (Windows doesn't have t honor it)

        if (_pipe = CreateNamedPipe(
            pipename.c_str(),             // pipe name 
            PIPE_ACCESS_DUPLEX,       // read/write access 
            PIPE_TYPE_MESSAGE |       // message type pipe 
            PIPE_READMODE_MESSAGE |   // message-read mode 
            PIPE_WAIT,                // blocking mode 
            PIPE_UNLIMITED_INSTANCES, // max. instances (255)
            buf_size,                  // output buffer size (default buffer size for Windows named pipes is 64 KB, above not guaranteed)
            buf_size,                  // input buffer size 
            500,                        // client time-out 
            nullptr);                    // default security attribute             

            _pipe == INVALID_HANDLE_VALUE)
        {
            std::string str_name = util::from_wstring(pipename);
            log("failed to create pipe: ", str_name, ": ", GetLastError());
            throw util::exception_t("failed to create pipe: " + str_name, GetLastError());
        }

        log("server created a pipe");

        // connect to client, ERROR_PIPE_CONNECTED means client connected before ConnectNamedPipe called
        // ConnectNamedPipe blocks indefinitely until connected or failed
        if (auto is_connected = ConnectNamedPipe(_pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED; !is_connected)
        {
            auto last_error = GetLastError();
            log("failed to connect to pipe: ", last_error);
            throw util::exception_t("failed to connect to pipe: ", last_error);
        }
        log("server connected client");
    }

    //----------------------------------------------------------------------------------------------
    template<class ...Fn>
    void start_server(const std::wstring& pipename, std::shared_ptr<util::logger> log, Fn&&... fn)
    {
        auto mutex_name = L"Local" + pipename.substr(pipename.find_last_of(L'\\'));
        if (auto h_mutex = CreateMutex(NULL, TRUE, mutex_name.c_str()); GetLastError() == ERROR_ALREADY_EXISTS) 
        {
            // Another instance is already running
            log->writeln("failed to start the server another instance is already running, mutex: ", util::from_wstring(mutex_name));
            CloseHandle(h_mutex);
        }
        else
        {
            using namespace std::chrono_literals;
            std::vector<std::future<int>> v;
            std::atomic_bool shutdown{ false }; // signals client requesting server shutdown
            std::atomic_bool noshutdown{ false }; // restrics where shutdown could happen
            // guarantees that there is one instance of server running after shutdown signal

            while (!shutdown && v.size() < 1000) // something is wrong if there are 1000 threads pending
            {
                // shutdown is allowed to be set
                noshutdown = false;
                noshutdown.notify_one();

                if (v.size() % 100 == 0) // clean up periodically
                    for (auto&& f : v)
                        std::erase_if(v, [](auto&& fut) { return fut.wait_for(0s) == std::future_status::ready; });

                winpipe_server_t server(pipename, log);
                std::atomic_bool move_completed{ false };
                noshutdown = true;
                // shutdown is not allowed to be set till the end of the loop

                auto f = std::async(std::launch::async,
                    [&] {
                        auto s{ std::move(server) };
                        move_completed = true;
                        move_completed.notify_one();
                        auto ret = s.run(std::forward<decltype(fn)>(fn)...);

                        if (ret == server_shutdown)
                        {
                            noshutdown.wait(true);
                            shutdown = true;
                        }

                        return ret;
                    });
                
                move_completed.wait(false);

                v.push_back(std::move(f));
            }

            if(h_mutex)
                CloseHandle(h_mutex);
        }
    }

    //----------------------------------------------------------------------------------------------
    inline void shutdown_server(const std::wstring& pipename, unsigned attempts, auto&&...log_args)
    {
        using namespace std::chrono_literals;
        auto mutex_name = L"Local" + pipename.substr(pipename.find_last_of(L'\\'));

        if (auto h_mutex = CreateMutex(NULL, TRUE, mutex_name.c_str()); GetLastError() == ERROR_ALREADY_EXISTS)
        {
            // shutdown sequence
            winpipe_client_t(pipename, "shutdown", attempts, log_args...).shutdown();
            // drain remaining instances, may fail if the previous client thread is too slow
            try { for(;;) winpipe_client_t(pipename, "drain", attempts, log_args...); }
            catch (...) {}

            if(h_mutex)
                CloseHandle(h_mutex);
        }
    }
}

#endif
