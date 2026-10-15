// Copyright (C) 2026, Gene Bushuyev. Boost Software License, Version 1.0.
#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace gb::yadro::container {
    // A syntactically valid document rejected by the gbdb value/archive model.
    // Keep event sequencing, options and other programming errors as logic_error.
    // Derived for existing callers which already catch that documented base type.
    class json_db_decode_error final : public std::logic_error {
    public:
        using std::logic_error::logic_error;
    };
    enum class blob_integrity_failure_kind { size_mismatch, digest_mismatch };
    struct blob_integrity_failure {
        std::filesystem::path path;
        blob_integrity_failure_kind kind;
        std::uint64_t expected_size{};
        std::uint64_t actual_size{};
        std::string expected_digest;
        std::string actual_digest;
    };
    class blob_integrity_error : public std::runtime_error {
    public:
        explicit blob_integrity_error(blob_integrity_failure failure)
            : std::runtime_error(failure.kind == blob_integrity_failure_kind::size_mismatch
                ? "gbdb external object blob size mismatch" : "gbdb external object blob MD5 mismatch"),
              failure_(std::move(failure)) {}
        [[nodiscard]] const blob_integrity_failure& detail() const noexcept { return failure_; }
    private:
        blob_integrity_failure failure_;
    };
}
