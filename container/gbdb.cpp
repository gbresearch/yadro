//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//-----------------------------------------------------------------------------

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>

#include "gbdb.h"

namespace gb::yadro::container
{

    // ------------------------------------------------------------
    // ANSI color helpers
    // ------------------------------------------------------------
    namespace term {
        static constexpr const char* reset = "\033[0m";
        static constexpr const char* bold = "\033[1m";
        static constexpr const char* dim = "\033[2m";

        static constexpr const char* red = "\033[31m";
        static constexpr const char* yellow = "\033[33m";
        static constexpr const char* green = "\033[32m";
        static constexpr const char* cyan = "\033[36m";
        static constexpr const char* white = "\033[37m";
        static constexpr const char* magenta = "\033[35m";
    }

    // ------------------------------------------------------------
    // Severity → string
    // ------------------------------------------------------------
    static std::string severity_to_string(json_db::scan_severity s)
    {
        switch (s)
        {
        case json_db::scan_severity::info:     return "info";
        case json_db::scan_severity::warning:  return "warning";
        case json_db::scan_severity::error:    return "error";
        case json_db::scan_severity::critical: return "critical";
        }
        return "unknown";
    }

    // ------------------------------------------------------------
    // Severity → colorized string
    // ------------------------------------------------------------
    static std::string colorize_severity(json_db::scan_severity s)
    {
        using namespace term;

        switch (s)
        {
        case json_db::scan_severity::info:
            return std::string(dim) + cyan + "info" + reset;
        case json_db::scan_severity::warning:
            return std::string(yellow) + "warning" + reset;
        case json_db::scan_severity::error:
            return std::string(red) + "error" + reset;
        case json_db::scan_severity::critical:
            return std::string(bold) + red + "critical" + reset;
        }
        return "unknown";
    }

    // ------------------------------------------------------------
    // JSON serialization helpers
    // ------------------------------------------------------------
    static void json_escape(std::ostream& os, const std::string& s)
    {
        for (char c : s)
        {
            switch (c)
            {
            case '\\': os << "\\\\"; break;
            case '"':  os << "\\\""; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:   os << c;      break;
            }
        }
    }

    void print_json_report(const json_db::scan_report& rep, std::ostream& os)
    {
        os << "{\n";

        os << "  \"ok\": " << (rep.ok() ? "true" : "false") << ",\n";
        os << "  \"cancelled\": " << (rep.cancelled ? "true" : "false") << ",\n";

        os << "  \"statistics\": {\n";
        const auto& st = rep.stats;

        auto stat = [&](std::string_view name, std::uint64_t v, bool last = false)
            {
                os << "    \"" << name << "\": " << v << (last ? "\n" : ",\n");
            };

        stat("node_count", st.node_count);
        stat("reachable_node_count", st.reachable_node_count);
        stat("orphaned_node_count", st.orphaned_node_count);
        stat("max_depth", st.max_depth);
        stat("value_count", st.value_count);
        stat("null_value_count", st.null_value_count);
        stat("bool_value_count", st.bool_value_count);
        stat("int_value_count", st.int_value_count);
        stat("uint_value_count", st.uint_value_count);
        stat("double_value_count", st.double_value_count);
        stat("string_value_count", st.string_value_count);
        stat("int_array_value_count", st.int_array_value_count);
        stat("uint_array_value_count", st.uint_array_value_count);
        stat("double_array_value_count", st.double_array_value_count);
        stat("string_array_value_count", st.string_array_value_count);
        stat("blob_value_count", st.blob_value_count);
        stat("object_value_count", st.object_value_count);
        stat("table_value_count", st.table_value_count);
        stat("string_count", st.string_count);
        stat("int_array_count", st.int_array_count);
        stat("uint_array_count", st.uint_array_count);
        stat("double_array_count", st.double_array_count);
        stat("string_array_count", st.string_array_count);
        stat("blob_count", st.blob_count);
        stat("object_count", st.object_count);
        stat("external_blob_count", st.external_blob_count);
        stat("table_count", st.table_count);
        stat("warning_count", st.warning_count);
        stat("error_count", st.error_count);
        stat("critical_count", st.critical_count, true);

        os << "  },\n";

        os << "  \"issues\": [\n";

        for (size_t i = 0; i < rep.issues.size(); ++i)
        {
            const auto& is = rep.issues[i];
            bool last = (i + 1 == rep.issues.size());

            os << "    {\n";
            os << "      \"severity\": \"" << severity_to_string(is.severity) << "\",\n";

            os << "      \"category\": \"";
            json_escape(os, is.category);
            os << "\",\n";

            os << "      \"path\": \"";
            json_escape(os, is.path);
            os << "\",\n";

            os << "      \"message\": \"";
            json_escape(os, is.message);
            os << "\"\n";

            os << "    }" << (last ? "\n" : ",\n");
        }

        os << "  ]\n";
        os << "}\n";
    }

    static void pretty_print(const json_db::scan_statistics& st, std::ostream& os)
    {
        using namespace term;

        os << bold << "=== Scan Statistics ===" << reset << "\n";

        auto row = [&](std::string_view name, std::uint64_t v)
            {
                os << "  " << std::left << std::setw(28) << name
                    << " : " << v << "\n";
            };

        row("node_count", st.node_count);
        row("reachable_node_count", st.reachable_node_count);
        row("orphaned_node_count", st.orphaned_node_count);
        row("max_depth", st.max_depth);

        os << "\n  -- Value Counters --\n";
        row("value_count", st.value_count);
        row("null_value_count", st.null_value_count);
        row("bool_value_count", st.bool_value_count);
        row("int_value_count", st.int_value_count);
        row("uint_value_count", st.uint_value_count);
        row("double_value_count", st.double_value_count);
        row("string_value_count", st.string_value_count);
        row("int_array_value_count", st.int_array_value_count);
        row("uint_array_value_count", st.uint_array_value_count);
        row("double_array_value_count", st.double_array_value_count);
        row("string_array_value_count", st.string_array_value_count);
        row("blob_value_count", st.blob_value_count);
        row("object_value_count", st.object_value_count);
        row("table_value_count", st.table_value_count);

        os << "\n  -- Object Counters --\n";
        row("string_count", st.string_count);
        row("int_array_count", st.int_array_count);
        row("uint_array_count", st.uint_array_count);
        row("double_array_count", st.double_array_count);
        row("string_array_count", st.string_array_count);
        row("blob_count", st.blob_count);
        row("object_count", st.object_count);
        row("external_blob_count", st.external_blob_count);
        row("table_count", st.table_count);

        os << "\n  -- Severity Counters --\n";
        row("warning_count", st.warning_count);
        row("error_count", st.error_count);
        row("critical_count", st.critical_count);

        os << "\n";
    }

    static void pretty_print(const json_db::scan_report& rep, std::ostream& os)
    {
        using namespace term;

        os << bold << "==============================\n";
        os << "        Scan Report\n";
        os << "==============================\n\n" << reset;

        if (rep.cancelled)
            os << magenta << bold << "STATUS: CANCELLED" << reset << "\n\n";
        else if (rep.ok())
            os << green << bold << "STATUS: OK" << reset << "\n\n";
        else
            os << red << bold << "STATUS: ISSUES FOUND" << reset << "\n\n";

        pretty_print(rep.stats, os);

        os << bold << "=== Issues (" << rep.issues.size() << ") ===" << reset << "\n";

        if (rep.issues.empty())
        {
            os << "  No issues.\n";
            return;
        }

        constexpr int sev_w = 12;
        constexpr int cat_w = 20;
        constexpr int path_w = 36;

        os << std::left
            << "  " << std::setw(sev_w) << "Severity"
            << " | " << std::setw(cat_w) << "Category"
            << " | " << std::setw(path_w) << "Path"
            << " | Message\n";

        os << "  " << std::string(sev_w, '-')
            << "-+-" << std::string(cat_w, '-')
            << "-+-" << std::string(path_w, '-')
            << "-+-" << std::string(40, '-') << "\n";

        for (const auto& issue : rep.issues)
        {
            std::string sev = colorize_severity(issue.severity);
            std::string cat = std::string(term::cyan) + issue.category + term::reset;
            std::string path = std::string(term::dim) + issue.path + term::reset;
            std::string msg = std::string(term::white) + issue.message + term::reset;

            os << "  " << std::setw(sev_w) << sev
                << " | " << std::setw(cat_w) << cat
                << " | " << std::setw(path_w) << path
                << " | " << msg << "\n";
        }

        os << "\n";
    }

    // ------------------------------------------------------------
    // Colorized pretty printer (your existing version)
    // ------------------------------------------------------------
    void print_color_report(const json_db::scan_report& rep, std::ostream& os)
    {
        using namespace term;

        os << bold << "==============================\n";
        os << "        Scan Report\n";
        os << "==============================\n\n" << reset;

        if (rep.cancelled)
            os << magenta << bold << "STATUS: CANCELLED" << reset << "\n\n";
        else if (rep.ok())
            os << green << bold << "STATUS: OK" << reset << "\n\n";
        else
            os << red << bold << "STATUS: ISSUES FOUND" << reset << "\n\n";

        // Reuse your existing pretty_print(stats)
        pretty_print(rep.stats, os);

        os << term::bold << "=== Issues (" << rep.issues.size() << ") ===" << term::reset << "\n";

        if (rep.issues.empty())
        {
            os << "  No issues.\n";
            return;
        }

        constexpr int sev_w = 12;
        constexpr int cat_w = 20;
        constexpr int path_w = 36;

        os << std::left
            << "  " << std::setw(sev_w) << "Severity"
            << " | " << std::setw(cat_w) << "Category"
            << " | " << std::setw(path_w) << "Path"
            << " | Message\n";

        os << "  " << std::string(sev_w, '-')
            << "-+-" << std::string(cat_w, '-')
            << "-+-" << std::string(path_w, '-')
            << "-+-" << std::string(40, '-') << "\n";

        for (const auto& issue : rep.issues)
        {
            std::string sev = colorize_severity(issue.severity);
            std::string cat = std::string(term::cyan) + issue.category + term::reset;
            std::string path = std::string(term::dim) + issue.path + term::reset;
            std::string msg = std::string(term::white) + issue.message + term::reset;

            os << "  " << std::setw(sev_w) << sev
                << " | " << std::setw(cat_w) << cat
                << " | " << std::setw(path_w) << path
                << " | " << msg << "\n";
        }

        os << "\n";
    }
}
