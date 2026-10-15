//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//-----------------------------------------------------------------------------

#pragma once

#include "gbdb.h"
#include "../util/durable_file.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <stdexcept>

namespace gb::yadro::container
{
    [[nodiscard]] inline json_db load_json_db_file(const std::filesystem::path& file)
    {
        json_db db;
        gb::yadro::util::checked_file_input in{file};
        try { db.serialize(gb::yadro::archive::bin_archive{ in }); }
        catch (...) { in.rethrow_read_failure(); throw; }
        in.rethrow_read_failure();
        return db;
    }

    // Writes db to file durably and atomically (see util/durable_file.h): after a crash, file
    // holds the complete previous database or the complete new one. Failures propagate: a
    // util::replace_not_durable_error means the new database is already in place but its
    // durability is unconfirmed, a util::replace_outcome_unknown_error means it may or may not
    // be, and any other exception means the previous file is untouched.
    inline void save_json_db_file(const json_db& db, const std::filesystem::path& file)
    {
        if (auto parent = file.parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);

        gb::yadro::util::atomic_replace_file(file, [&db](std::ostream& out) {
            const_cast<json_db&>(db).serialize(gb::yadro::archive::bin_archive{ out });
        });
    }
}
