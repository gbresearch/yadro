// Copyright (C) 2026, Gene Bushuyev. Boost Software License, Version 1.0.
#pragma once

#include "gbwin.h"
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <istream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <system_error>
#include <utility>
#if !defined(GBWINDOWS)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gb::yadro::util {
    enum class file_read_operation { open, read };
    struct file_read_failure {
        std::filesystem::path path;
        file_read_operation operation;
        std::error_code native_error;
    };

    class file_read_error : public std::runtime_error {
    public:
        explicit file_read_error(file_read_failure failure)
            : std::runtime_error(describe(failure)),
              failure_(std::move(failure)) {}
        [[nodiscard]] const file_read_failure& detail() const noexcept { return failure_; }
    private:
        static std::string describe(const file_read_failure& failure) {
            const auto utf8 = failure.path.generic_u8string();
            return std::string{"file "} + (failure.operation == file_read_operation::open ? "open" : "read")
                + " failed: " + std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()}
                + " (" + failure.native_error.category().name() + " "
                + std::to_string(failure.native_error.value()) + "): " + failure.native_error.message();
        }
        file_read_failure failure_;
    };

    namespace detail {
        using file_read_fault = std::function<void(const std::filesystem::path&, file_read_operation, std::uint64_t)>;
        inline thread_local file_read_fault read_fault;

        // Scoped and thread-local like the durable writer's fault seam. No product setting enables it.
        class scoped_file_read_fault {
        public:
            explicit scoped_file_read_fault(file_read_fault fault)
                : previous_(std::exchange(read_fault, std::move(fault))) {}
            scoped_file_read_fault(const scoped_file_read_fault&) = delete;
            scoped_file_read_fault& operator=(const scoped_file_read_fault&) = delete;
            ~scoped_file_read_fault() { read_fault = std::move(previous_); }
        private:
            file_read_fault previous_;
        };

        class checked_read_buffer final : public std::streambuf {
        public:
            explicit checked_read_buffer(const std::filesystem::path& path) : path_(path) {
                if (read_fault) read_fault(path_, file_read_operation::open, 0);
#if defined(GBWINDOWS)
                handle_.reset(CreateFileW(path_.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (!handle_.valid()) {
                    const auto error = GetLastError();
                    throw file_read_error{{path_, file_read_operation::open,
                        std::error_code{static_cast<int>(error), std::system_category()}}};
                }
#else
                do { fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC); } while (fd_ < 0 && errno == EINTR);
                if (fd_ < 0) {
                    const auto error = errno;
                    throw file_read_error{{path_, file_read_operation::open,
                        std::error_code{error, std::generic_category()}}};
                }
#endif
            }
            ~checked_read_buffer() override {
#if !defined(GBWINDOWS)
                if (fd_ >= 0) (void)::close(fd_);
#endif
            }
            checked_read_buffer(const checked_read_buffer&) = delete;
            checked_read_buffer& operator=(const checked_read_buffer&) = delete;

            void rethrow_read_failure() const {
                if (failure_) std::rethrow_exception(failure_);
            }
        protected:
            int_type underflow() override {
                rethrow_read_failure();
                if (gptr() != egptr()) return traits_type::to_int_type(*gptr());
                if (eof_) return traits_type::eof();
                try {
                    if (read_fault) read_fault(path_, file_read_operation::read, offset_);
                    std::size_t count{};
#if defined(GBWINDOWS)
                    DWORD received{};
                    if (!ReadFile(handle_.get(), buffer_.data(), static_cast<DWORD>(buffer_.size()), &received, nullptr)) {
                        const auto error = GetLastError();
                        if (error != ERROR_HANDLE_EOF)
                            throw file_read_error{{path_, file_read_operation::read,
                                std::error_code{static_cast<int>(error), std::system_category()}}};
                    }
                    count = received;
#else
                    ssize_t received{};
                    do { received = ::read(fd_, buffer_.data(), buffer_.size()); } while (received < 0 && errno == EINTR);
                    if (received < 0) {
                        const auto error = errno;
                        throw file_read_error{{path_, file_read_operation::read,
                            std::error_code{error, std::generic_category()}}};
                    }
                    count = static_cast<std::size_t>(received);
#endif
                    if (count == 0) { eof_ = true; return traits_type::eof(); }
                    offset_ += count;
                    setg(buffer_.data(), buffer_.data(), buffer_.data() + count);
                    return traits_type::to_int_type(*gptr());
                }
                catch (...) {
                    failure_ = std::current_exception();
                    throw;
                }
            }
        private:
            std::filesystem::path path_;
#if defined(GBWINDOWS)
            unique_win_handle handle_;
#else
            int fd_{-1};
#endif
            std::array<char, 8192> buffer_{};
            std::uint64_t offset_{};
            std::exception_ptr failure_;
            bool eof_{};
        };
    }

    class checked_file_input final : public std::istream {
    public:
        explicit checked_file_input(const std::filesystem::path& path)
            : std::istream(nullptr), buffer_(path) {
            rdbuf(&buffer_);
            exceptions(std::ios::badbit);
        }
        ~checked_file_input() override = default;
        checked_file_input(const checked_file_input&) = delete;
        checked_file_input& operator=(const checked_file_input&) = delete;
        checked_file_input(checked_file_input&&) = delete;
        checked_file_input& operator=(checked_file_input&&) = delete;
        void rethrow_read_failure() const { buffer_.rethrow_read_failure(); }
    private:
        detail::checked_read_buffer buffer_;
    };
}
