// Copyright (C) 2026, Gene Bushuyev. Boost Software License, Version 1.0.
#include "../util/gbtest.h"
#include "../util/gbwin.h"
#include "../container/gbdb_json.h"
#include "../container/gbdb_file.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>
#include "../util/checked_file_read.h"
#include "../container/gbdb_read_error.h"

namespace {
    namespace fs = std::filesystem;
    using namespace gb::yadro::container;
    using namespace gb::yadro::util;
    struct fixture {
        fs::path root;
        fixture() {
            static std::atomic<unsigned> counter{};
            root = fs::temp_directory_path() / ("yadro_checked_read_" + std::to_string(
#if defined(GBWINDOWS)
                GetCurrentProcessId()
#else
                getpid()
#endif
            ) + "_" + std::to_string(counter++));
            gbassert(fs::create_directory(root));
        }
        ~fixture() {
            std::error_code ec;
            if (root.parent_path() == fs::temp_directory_path()) fs::remove_all(root, ec);
        }
        fs::path file(std::string_view name) const { return root / name; }
    };
    void write(const fs::path& p, std::string_view bytes) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        gbassert(static_cast<bool>(out));
    }
    std::string bytes(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        gbassert(static_cast<bool>(in));
        return {std::istreambuf_iterator<char>{in}, {}};
    }
    template<class Fn> void expect_io(Fn&& fn, const fs::path& p, int native) {
        bool matched{};
        std::string observed = "no exception";
        try { fn(); }
        catch (const std::exception& e) {
            observed = e.what();
            if (const auto* failure = dynamic_cast<const file_read_error*>(&e)) {
                matched = failure->detail().path.lexically_normal() == p.lexically_normal()
                    && failure->detail().native_error.value() == native
                    && failure->detail().operation == file_read_operation::open;
#if defined(GBWINDOWS)
                gbassert(failure->detail().native_error.category() == std::system_category());
#else
                gbassert(failure->detail().native_error.category() == std::generic_category());
#endif
            }
        }
        gbassert(matched, "Expected typed native open failure for " + p.string() + "; observed: " + observed);
    }
    template<class Fn> void expect_integrity(Fn&& fn, const fs::path& p, bool digest) {
        bool matched{};
        std::string observed = "no exception";
        try { fn(); }
        catch (const std::exception& e) {
            observed = e.what();
            if (const auto* failure = dynamic_cast<const blob_integrity_error*>(&e)) {
                const auto& d = failure->detail();
                matched = d.path.lexically_normal() == p.lexically_normal() && d.kind == (digest
                    ? blob_integrity_failure_kind::digest_mismatch : blob_integrity_failure_kind::size_mismatch);
                if (digest) gbassert(d.expected_digest != d.actual_digest && d.expected_size == d.actual_size);
                else gbassert(d.expected_size != d.actual_size);
            }
        }
        gbassert(matched, "Expected typed blob integrity failure; observed: " + observed);
    }
    fs::path seed_blob(const fixture& f) {
        json_db db;
        db.set_object("payload", std::vector<std::uint64_t>{3, 5, 8}, "read_test", 1);
        json_write_options options;
        options.blob_write_mode = json_blob_write_mode::external_files;
        options.external_blobs.root_directory = f.root;
        options.external_blobs.blob_directory = "blobs";
        options.external_blobs.inline_threshold_bytes = 0;
        (void)export_json_file(db, f.file("chart.json"), options);
        std::vector<fs::path> blobs;
        for (const auto& entry : fs::directory_iterator(f.file("blobs")))
            if (entry.is_regular_file()) blobs.push_back(entry.path());
        gbassert(blobs.size() == 1);
        return blobs.front();
    }
    void load_blob(const fixture& f, json_db::blob_load_policy policy) {
        json_read_options options;
        options.blob_mode = json_blob_mode::tagged_base64;
        options.external_blob_base_directory = f.root;
        options.external_blob_load_policy = policy;
        auto db = read_json_file(f.file("chart.json"), options);
        auto* value = db.get("payload"); gbassert(value != nullptr);
        std::vector<std::uint64_t> result;
        db.object(std::get<json_db::object_ref>(*value), result);
        gbassert((result == std::vector<std::uint64_t>{3, 5, 8}));
    }
}

GB_TEST(file_read, checked_read_preserves_native_open_and_read_failure) {
    fixture f;
    auto missing = f.file("absent.json");
    expect_io([&] { (void)read_json_file(missing); }, missing,
#if defined(GBWINDOWS)
        ERROR_FILE_NOT_FOUND
#else
        ENOENT
#endif
    );
    expect_io([&] { (void)load_json_db_file(missing); }, missing,
#if defined(GBWINDOWS)
        ERROR_FILE_NOT_FOUND
#else
        ENOENT
#endif
    );
    const auto p = f.file("input.bin"); write(p, std::string(20000, 'x'));
    for (const int mode : {0, 1, 2, 3}) {
        gb::yadro::util::detail::scoped_file_read_fault fault{[&](const fs::path& path, file_read_operation op, std::uint64_t offset) {
            if (path != p || op != file_read_operation::read || offset == 0) return;
            if (mode == 1) throw std::bad_alloc{};
            if (mode == 2) throw std::logic_error("unexpected test reader failure");
            if (mode == 3) throw 17;
            throw file_read_error{{path, op, std::error_code{1234, std::system_category()}}};
        }};
        checked_file_input in{p};
        try { const std::string discarded{std::istreambuf_iterator<char>{in}, {}}; }
        catch (...) {} // Simulate a virtual decoder swallowing a failure.
        bool matched{};
        try { in.rethrow_read_failure(); }
        catch (const file_read_error& e) { matched = mode == 0 && e.detail().native_error.value() == 1234
            && e.detail().operation == file_read_operation::read && e.detail().path == p; }
        catch (const std::bad_alloc&) { matched = mode == 1; }
        catch (const std::logic_error&) { matched = mode == 2; }
        catch (int value) { matched = mode == 3 && value == 17; }
        gbassert(matched, "A swallowed failure must remain sticky, including its original exception type");
        // A retry after clear() cannot resume beyond a failed read.
        in.clear(); bool repeated{};
        try { (void)in.get(); } catch (...) { repeated = true; }
        gbassert(repeated);
    }
    { checked_file_input in{p}; std::string all{std::istreambuf_iterator<char>{in}, {}};
      in.rethrow_read_failure(); gbassert(all == std::string(20000, 'x')); }
    write(p, "");
    { checked_file_input in{p}; gbassert(in.get() == std::char_traits<char>::eof()); in.rethrow_read_failure(); }
#if defined(GBWINDOWS)
    unique_win_handle lock{CreateFileW(p.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
    gbassert(lock.get() != INVALID_HANDLE_VALUE);
    expect_io([&] { checked_file_input in{p}; }, p, ERROR_SHARING_VIOLATION);
#endif
}

GB_TEST(file_read, checked_read_fault_scope_and_binary_reader_contract) {
    fixture f;
    const auto p = f.file("database.bin");
    json_db db; db.set("text", std::string(20000, 'a'));
    save_json_db_file(db, p);
    auto restored = load_json_db_file(p);
    gbassert(restored.contains("text"));
    unsigned outer_calls{};
    gb::yadro::util::detail::scoped_file_read_fault outer{[&](const fs::path&, file_read_operation op, std::uint64_t) {
        if (op == file_read_operation::open) ++outer_calls;
    }};
    {
        gb::yadro::util::detail::scoped_file_read_fault inner{[&](const fs::path& path, file_read_operation op, std::uint64_t offset) {
            if (path == p && op == file_read_operation::read && offset > 0)
                throw file_read_error{{path, op, std::error_code{1234, std::system_category()}}};
        }};
        bool native{};
        try { (void)load_json_db_file(p); }
        catch (const file_read_error& error) {
            native = error.detail().path == p && error.detail().operation == file_read_operation::read
                && error.detail().native_error.value() == 1234;
        }
        gbassert(native);
    }
    (void)load_json_db_file(p);
    gbassert(outer_calls == 1, "Nested hook scope must restore the previous hook");
}

GB_TEST_IF(gbdb_json_axe_enabled, file_read, gbdb_both_readers_distinguish_io_from_integrity) {
    for (const auto policy : {json_db::blob_load_policy::immediate, json_db::blob_load_policy::deferred}) {
        fixture f;
        auto blob = seed_blob(f);
        const auto original = bytes(blob);
        load_blob(f, policy);
        {
            gb::yadro::util::detail::scoped_file_read_fault fault{[&](const fs::path& path, file_read_operation op, std::uint64_t) {
                if (path.lexically_normal() == blob.lexically_normal() && op == file_read_operation::read)
                    throw file_read_error{{path, op, std::error_code{1234, std::system_category()}}};
            }};
            bool native{};
            try { load_blob(f, policy); }
            catch (const file_read_error& error) {
                native = error.detail().path.lexically_normal() == blob.lexically_normal()
                    && error.detail().operation == file_read_operation::read
                    && error.detail().native_error.value() == 1234;
            }
            gbassert(native, "Both blob readers must preserve a native read failure instead of reporting integrity damage");
        }
        write(blob, std::string_view{original}.substr(0, original.size() - 1));
        expect_integrity([&] { load_blob(f, policy); }, blob, false);
        auto tampered = original; tampered.back() ^= 1; write(blob, tampered);
        expect_integrity([&] { load_blob(f, policy); }, blob, true);
        write(blob, original);
#if defined(GBWINDOWS)
        {
            unique_win_handle lock{CreateFileW(blob.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
            gbassert(lock.valid());
            expect_io([&] { load_blob(f, policy); }, blob, ERROR_SHARING_VIOLATION);
        }
#endif
        fs::remove(blob);
        expect_io([&] { load_blob(f, policy); }, blob,
#if defined(GBWINDOWS)
            ERROR_FILE_NOT_FOUND
#else
            ENOENT
#endif
        );
    }
}

GB_TEST_IF(gbdb_json_axe_enabled, file_read, checked_stream_preserves_eof_and_capped_json_contract) {
    fixture f;
    const auto p = f.file("document.json");
    const std::string document = "{\n\"value\":42}";
    write(p, document);
    checked_file_input in{p};
    const std::string all{std::istreambuf_iterator<char>{in}, {}}; gbassert(all == document);
    in.rethrow_read_failure();
    json_read_options options;
    options.max_input_bytes = document.size();
    (void)read_json_file(p, options);
    {
        gb::yadro::util::detail::scoped_file_read_fault fault{[&](const fs::path& path, file_read_operation op, std::uint64_t) {
            if (path == p && op == file_read_operation::read)
                throw file_read_error{{path, op, std::error_code{1234, std::system_category()}}};
        }};
        bool native{};
        try { (void)read_json_file(p, options); }
        catch (const file_read_error& error) {
            native = error.detail().path == p && error.detail().operation == file_read_operation::read
                && error.detail().native_error.value() == 1234;
        }
        gbassert(native, "A JSON read failure must not become a parser or cap failure");
    }
    options.max_input_bytes = document.size() - 1;
    bool capped{};
    try { (void)read_json_file(p, options); }
    catch (const json_parse_error& error) {
        const auto& d = error;
        capped = d.code == json_parse_errc::input_too_large && d.offset == document.size() - 1
            && d.line == 2 && d.column == 11;
    }
    gbassert(capped);
    write(p, "");
    bool empty_is_parse_error{};
    try { (void)read_json_file(p); }
    catch (const json_parse_error& error) { empty_is_parse_error = error.offset == 0; }
    gbassert(empty_is_parse_error);
}
