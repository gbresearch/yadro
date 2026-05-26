//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//-----------------------------------------------------------------------------

#pragma once

#include "gbdb.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace gb::yadro::container
{
    [[nodiscard]] inline json_db load_json_db_file(const std::filesystem::path& file)
    {
        json_db db;
        std::ifstream in(file, std::ios::binary);
        if (!in)
            throw std::runtime_error("failed to open database file for reading: " + file.string());
        db.serialize(gb::yadro::archive::bin_archive{ in });
        return db;
    }

    inline void save_json_db_file(const json_db& db, const std::filesystem::path& file)
    {
        auto temp = file;
        temp += ".tmp";
        if (auto parent = file.parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);

        {
            std::ofstream out(temp, std::ios::binary);
            if (!out)
                throw std::runtime_error("failed to open database file for writing: " + temp.string());
            const_cast<json_db&>(db).serialize(gb::yadro::archive::bin_archive{ out });
            if (!out)
                throw std::runtime_error("failed to write database file: " + temp.string());
        }

        std::error_code ec;
        std::filesystem::rename(temp, file, ec);
        if (ec) {
            std::filesystem::remove(file, ec);
            ec.clear();
            std::filesystem::rename(temp, file, ec);
            if (ec)
                throw std::runtime_error("failed to replace database file: " + file.string());
        }
    }
}
