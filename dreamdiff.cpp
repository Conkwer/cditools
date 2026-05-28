// dreamdiff - Compare two Dreamcast database files and generate diff reports
// Build: g++ -std=c++17 -Wall -Wextra -O2 dreamdiff.cpp -o dreamdiff
// Platform: Linux GCC / Windows MinGW-w64

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class FileType : uint8_t {
    Unknown = 0,
    Boot,    // orange  - 1st_read.bin, ip.bin, *.exe, *winceos.bin
    Archive, // blue    - *.afs, *.pod
    Audio    // green   - *.adx, *.pcm
};

enum class DiffStatus : uint8_t {
    Identical, // same hash both sides
    Changed,   // different hash
    OnlyLeft,  // only in first file (Kudos)
    OnlyRight  // only in second file (Vector)
};

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct FileEntry {
    std::string path;
    uint64_t    size = 0;
    std::string hash;
    FileType    type = FileType::Unknown;
};

struct DiffEntry {
    std::string path;
    uint64_t    size       = 0;
    std::string left_hash;
    std::string right_hash;
    DiffStatus  status = DiffStatus::Identical;
    FileType    type   = FileType::Unknown;
};

struct DiffResult {
    std::vector<DiffEntry> entries;
    int      identical_count = 0;
    int      changed_count   = 0;
    int      only_left_count = 0;
    int      only_right_count = 0;
    uint64_t left_total_size  = 0;
    uint64_t right_total_size = 0;
};

struct Options {
    std::string file1;
    std::string file2;
    std::string html_output = "difference.html";
    std::string md_output;
    std::string title;
    std::string left_label;
    std::string right_label;
    bool show_help = false;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool ends_with(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string extract_filename(const std::string& path) {
    auto sep = path.find_last_of("\\/");
    return (sep == std::string::npos) ? path : path.substr(sep + 1);
}

static FileType classify_file(const std::string& path) {
    std::string fn = to_lower(extract_filename(path));

    // Boot / executable
    if (fn == "1st_read.bin" || fn == "ip.bin") return FileType::Boot;
    if (ends_with(fn, ".exe")) return FileType::Boot;
    if (ends_with(fn, "winceos.bin")) return FileType::Boot;

    // Archives
    if (ends_with(fn, ".afs")) return FileType::Archive;
    if (ends_with(fn, ".pod")) return FileType::Archive;

    // Audio
    if (ends_with(fn, ".adx")) return FileType::Audio;
    if (ends_with(fn, ".pcm")) return FileType::Audio;

    return FileType::Unknown;
}

static const char* file_type_css_class(FileType t) {
    switch (t) {
        case FileType::Boot:    return "boot";
        case FileType::Archive: return "archive";
        case FileType::Audio:   return "audio";
        default:                return "";
    }
}

static const char* file_type_color(FileType t) {
    switch (t) {
        case FileType::Boot:    return "orange";
        case FileType::Archive: return "dodgerblue";
        case FileType::Audio:   return "mediumseagreen";
        default:                return "";
    }
}

static std::string escape_html(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  r += "&amp;";  break;
            case '<':  r += "&lt;";   break;
            case '>':  r += "&gt;";   break;
            case '"':  r += "&quot;"; break;
            default:   r += c;
        }
    }
    return r;
}

static std::string format_number(uint64_t n) {
    std::string s = std::to_string(n);
    int pos = static_cast<int>(s.size()) - 3;
    while (pos > 0) {
        s.insert(static_cast<size_t>(pos), ",");
        pos -= 3;
    }
    return s;
}

// Generate a note for a changed file based on its name/type
static std::string changed_file_note(const DiffEntry& e) {
    std::string fn = to_lower(extract_filename(e.path));

    if (fn == "dc.exe" || fn == "1st_read.bin")
        return "main executable; recompiled or patched with different code.";
    if (ends_with(fn, ".exe"))
        return "executable; recompiled or patched with different code.";
    if (fn.find("english") != std::string::npos || fn.find("lang") != std::string::npos)
        return "localization text strings; re-translated or revised between releases.";
    if (fn.find("ui") != std::string::npos || fn.find("menu") != std::string::npos)
        return "UI assets/menus; modified user interface.";
    if (fn.find("startup") != std::string::npos || fn.find("intro") != std::string::npos)
        return "startup assets (logos, splash screens); different intro branding.";
    if (fn.find("level") != std::string::npos || fn.find("track") != std::string::npos)
        return "level/track data; re-exported or patched levels.";
    if (fn.find("parts") != std::string::npos || fn.find("vehicle") != std::string::npos)
        return "vehicle parts data; rebalanced or modified models.";
    if (fn.find("series") != std::string::npos || fn.find("career") != std::string::npos)
        return "series/career mode data; updated career assets.";
    if (e.type == FileType::Archive)
        return "game data archive; content modified between releases.";
    if (e.type == FileType::Audio)
        return "audio data; re-encoded or replaced between releases.";
    return "file content differs between releases.";
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

static std::vector<FileEntry> parse_database(const std::string& filename,
                                             uint64_t& total_size) {
    std::vector<FileEntry> entries;
    std::ifstream in(filename);
    if (!in) {
        std::cerr << "Error: cannot open file: " << filename << std::endl;
        std::exit(1);
    }

    std::string line;
    int line_no = 0;

    // Parse header line
    if (!std::getline(in, line)) {
        std::cerr << "Error: file is empty: " << filename << std::endl;
        std::exit(1);
    }
    line_no = 1;

    // Strip \r
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Match "Total size: <num> bytes."
    {
        const std::string prefix = "Total size: ";
        if (line.size() < prefix.size() || line.substr(0, prefix.size()) != prefix) {
            std::cerr << "Error: missing 'Total size:' header on line 1 of "
                      << filename << std::endl;
            std::exit(1);
        }
        auto pos = line.find(" bytes");
        if (pos == std::string::npos) {
            std::cerr << "Error: malformed header in " << filename << std::endl;
            std::exit(1);
        }
        std::string num = line.substr(prefix.size(), pos - prefix.size());
        try {
            total_size = std::stoull(num);
        } catch (...) {
            std::cerr << "Error: invalid total size in " << filename << std::endl;
            std::exit(1);
        }
    }

    // Parse file entries
    while (std::getline(in, line)) {
        line_no++;

        // Strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip empty lines
        if (line.empty()) continue;

        // Format: <path> [<size> bytes] - <HASH>
        auto bracket = line.find(" [");
        if (bracket == std::string::npos) {
            std::cerr << "Warning: " << filename << ":" << line_no
                      << " - malformed line (no ' ['), skipping: "
                      << line << std::endl;
            continue;
        }

        std::string path = line.substr(0, bracket);

        auto bytes_pos = line.find(" bytes] - ", bracket);
        if (bytes_pos == std::string::npos) {
            std::cerr << "Warning: " << filename << ":" << line_no
                      << " - malformed line (no ' bytes] - '), skipping: "
                      << line << std::endl;
            continue;
        }

        std::string size_str = line.substr(bracket + 2,
                                           bytes_pos - bracket - 2);
        std::string hash = line.substr(bytes_pos + 10); // " bytes] - " is 10 chars

        // Validate hash is exactly 8 hex chars
        if (hash.size() != 8) {
            std::cerr << "Warning: " << filename << ":" << line_no
                      << " - hash is not 8 chars, skipping: " << line << std::endl;
            continue;
        }
        bool valid = true;
        for (char c : hash) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) { valid = false; break; }
        }
        if (!valid) {
            std::cerr << "Warning: " << filename << ":" << line_no
                      << " - invalid hash chars, skipping: " << line << std::endl;
            continue;
        }

        FileEntry entry;
        entry.path = path;
        entry.hash = hash;
        try {
            entry.size = std::stoull(size_str);
        } catch (...) {
            std::cerr << "Warning: " << filename << ":" << line_no
                      << " - invalid size, skipping: " << line << std::endl;
            continue;
        }
        entry.type = classify_file(path);
        entries.push_back(std::move(entry));
    }

    return entries;
}

// ---------------------------------------------------------------------------
// Comparator
// ---------------------------------------------------------------------------

static DiffResult compare_databases(
    const std::vector<FileEntry>& left,
    const std::vector<FileEntry>& right,
    uint64_t left_total,
    uint64_t right_total) {

    DiffResult result;
    result.left_total_size  = left_total;
    result.right_total_size = right_total;

    // Build maps: path -> entry
    std::map<std::string, const FileEntry*> left_map, right_map;
    for (const auto& e : left) {
        if (left_map.count(e.path)) {
            std::cerr << "Warning: duplicate path '" << e.path
                      << "' in first file, using last occurrence." << std::endl;
        }
        left_map[e.path] = &e;
    }
    for (const auto& e : right) {
        if (right_map.count(e.path)) {
            std::cerr << "Warning: duplicate path '" << e.path
                      << "' in second file, using last occurrence." << std::endl;
        }
        right_map[e.path] = &e;
    }

    // Collect all paths in order
    std::map<std::string, int> all_paths; // path -> 1=left, 2=right, 3=both
    for (const auto& kv : left_map)  all_paths[kv.first] |= 1;
    for (const auto& kv : right_map) all_paths[kv.first] |= 2;

    for (const auto& kv : all_paths) {
        const std::string& path = kv.first;
        int where = kv.second;

        DiffEntry de;
        de.path = path;

        if (where == 3) {
            // In both
            auto* le = left_map[path];
            auto* re = right_map[path];
            de.type = le->type; // prefer left type
            de.size = le->size; // prefer left size
            de.left_hash  = le->hash;
            de.right_hash = re->hash;
            if (le->hash == re->hash) {
                de.status = DiffStatus::Identical;
                result.identical_count++;
            } else {
                de.status = DiffStatus::Changed;
                result.changed_count++;
            }
        } else if (where == 1) {
            auto* le = left_map[path];
            de.type = le->type;
            de.size = le->size;
            de.left_hash = le->hash;
            de.status = DiffStatus::OnlyLeft;
            result.only_left_count++;
        } else {
            auto* re = right_map[path];
            de.type = re->type;
            de.size = re->size;
            de.right_hash = re->hash;
            de.status = DiffStatus::OnlyRight;
            result.only_right_count++;
        }

        result.entries.push_back(std::move(de));
    }

    return result;
}

// ---------------------------------------------------------------------------
// HTML Generator
// ---------------------------------------------------------------------------

static const char* HTML_CSS = R"CSS(
  :root {
    --bg: #1a1a2e;
    --surface: #16213e;
    --text: #e0e0e0;
    --muted: #8899aa;
    --border: #2a2a4a;
    --orange: #f0a040;
    --orange-bg: #3d2a10;
    --blue: #5a9cf0;
    --blue-bg: #101a30;
    --green: #4cbd80;
    --green-bg: #102a18;
    --diff-bg: #3d1a1a;
    --diff-border: #c04040;
    --same-bg: #1a2a1a;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'Segoe UI', 'Consolas', monospace;
    background: var(--bg);
    color: var(--text);
    padding: 32px;
    line-height: 1.5;
  }
  .container { max-width: 1100px; margin: 0 auto; }
  h1 { font-size: 1.6em; margin-bottom: 4px; color: #fff; }
  .subtitle { color: var(--muted); margin-bottom: 28px; font-size: 0.9em; }
  h2 { font-size: 1.2em; margin: 32px 0 12px; color: #ddd; border-bottom: 1px solid var(--border); padding-bottom: 6px; }
  h3 { font-size: 1.0em; margin: 20px 0 8px; color: #ccc; }

  .summary { display: flex; gap: 20px; margin-bottom: 24px; flex-wrap: wrap; }
  .summary-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 14px 22px;
    text-align: center;
    min-width: 100px;
  }
  .summary-card .num { font-size: 2em; font-weight: bold; }
  .summary-card .num.changed { color: #e07070; }
  .summary-card .label { font-size: 0.8em; color: var(--muted); }

  table { width: 100%; border-collapse: collapse; margin: 12px 0 20px; font-size: 0.85em; }
  th { background: var(--surface); color: var(--muted); font-weight: 600; text-align: left; padding: 8px 10px; border-bottom: 2px solid var(--border); font-size: 0.8em; text-transform: uppercase; letter-spacing: 0.5px; }
  td { padding: 6px 10px; border-bottom: 1px solid var(--border); }
  tr:hover { background: rgba(255,255,255,0.03); }

  .boot    { color: var(--orange); font-weight: bold; }
  .archive { color: var(--blue); }
  .audio   { color: var(--green); }

  tr.diff { background: var(--diff-bg); }
  tr.diff td:first-child { border-left: 3px solid var(--diff-border); }
  tr.diff:hover { background: #4a2020; }
  .diff-badge {
    display: inline-block;
    background: #c04040;
    color: #fff;
    font-size: 0.7em;
    font-weight: bold;
    padding: 2px 7px;
    border-radius: 3px;
    letter-spacing: 0.5px;
  }
  .same-badge {
    display: inline-block;
    color: #60a060;
    font-size: 0.85em;
  }
  .only-badge {
    display: inline-block;
    color: #c0a040;
    font-size: 0.75em;
    font-weight: bold;
  }

  .callout {
    padding: 10px 16px;
    border-radius: 4px;
    margin: 6px 0;
    font-size: 0.9em;
  }
  .callout.boot    { background: var(--orange-bg); border-left: 3px solid var(--orange); color: #f0c878; }
  .callout.archive { background: var(--blue-bg);  border-left: 3px solid var(--blue);  color: #88b8f0; }
  .callout.audio   { background: var(--green-bg); border-left: 3px solid var(--green); color: #78d8a0; }

  .legend { display: flex; gap: 24px; flex-wrap: wrap; margin: 10px 0 16px; font-size: 0.85em; }
  .legend-item { display: flex; align-items: center; gap: 6px; }
  .legend-swatch { width: 14px; height: 14px; border-radius: 3px; }
  .legend-swatch.orange { background: var(--orange); }
  .legend-swatch.blue   { background: var(--blue); }
  .legend-swatch.green  { background: var(--green); }
  .legend-swatch.diff   { background: var(--diff-border); }

  .notes { font-size: 0.85em; color: #bbb; margin: 8px 0 20px; }
  .notes li { margin: 4px 0; }
  .notes code { background: var(--surface); padding: 1px 5px; border-radius: 3px; font-size: 0.95em; }
  .hash { font-family: 'Consolas', 'Courier New', monospace; letter-spacing: 0.5px; }
  .size { color: var(--muted); font-size: 0.85em; }
  .dash { color: var(--muted); }
)CSS";

static void generate_html(const DiffResult& result, const std::string& filename,
                          const std::string& title, const std::string& left_label,
                          const std::string& right_label) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Error: cannot write to: " << filename << std::endl;
        std::exit(1);
    }

    auto status_badge = [](DiffStatus s) -> std::string {
        switch (s) {
            case DiffStatus::Identical: return "<span class=\"same-badge\">&#x2713;</span>";
            case DiffStatus::Changed:   return "<span class=\"diff-badge\">DIFF</span>";
            case DiffStatus::OnlyLeft:  return "<span class=\"only-badge\">" + std::string(escape_html("ONLY IN LEFT")) + "</span>";
            case DiffStatus::OnlyRight: return "<span class=\"only-badge\">" + std::string(escape_html("ONLY IN RIGHT")) + "</span>";
        }
        return "";
    };

    // Scan for presence of key file types
    bool has_1st_read = false, has_ip_bin = false, has_winceos = false;
    bool has_afs = false, has_pod = false;
    bool has_adx = false, has_pcm = false;
    for (const auto& e : result.entries) {
        std::string fn = to_lower(extract_filename(e.path));
        if (fn == "1st_read.bin") has_1st_read = true;
        if (fn == "ip.bin") has_ip_bin = true;
        if (ends_with(fn, "winceos.bin")) has_winceos = true;
        if (ends_with(fn, ".exe")) { /* counted in Boot */ }
        if (ends_with(fn, ".afs")) has_afs = true;
        if (ends_with(fn, ".pod")) has_pod = true;
        if (ends_with(fn, ".adx")) has_adx = true;
        if (ends_with(fn, ".pcm")) has_pcm = true;
    }
    bool has_boot_bin = has_1st_read || has_ip_bin || has_winceos;

    // Total size comparison
    std::string size_note = (result.left_total_size == result.right_total_size)
        ? "identical between releases"
        : "differs between releases";
    std::string size_info = format_number(result.left_total_size) + " bytes";
    if (result.left_total_size != result.right_total_size) {
        size_info += " (Kudos) vs " + format_number(result.right_total_size) + " bytes (Vector)";
    }

    // ---- Begin HTML ----
    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        << "<meta charset=\"UTF-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        << "<title>" << escape_html(title) << "</title>\n"
        << "<style>\n" << HTML_CSS << "\n</style>\n"
        << "</head>\n<body>\n<div class=\"container\">\n\n";

    // Title
    out << "<h1>" << escape_html(title) << "</h1>\n"
        << "<div class=\"subtitle\">Total size: " << size_info
        << " (" << size_note << ")</div>\n\n";

    // Summary
    out << "<h2>Summary</h2>\n<div class=\"summary\">\n";
    out << "  <div class=\"summary-card\"><div class=\"num\">"
        << result.identical_count
        << "</div><div class=\"label\">Identical files</div></div>\n";
    out << "  <div class=\"summary-card\"><div class=\"num"
        << (result.changed_count > 0 ? " changed" : "")
        << "\">" << result.changed_count
        << "</div><div class=\"label\">Changed files</div></div>\n";
    out << "  <div class=\"summary-card\"><div class=\"num\">"
        << result.only_left_count
        << "</div><div class=\"label\">" << escape_html(left_label) << "-only</div></div>\n";
    out << "  <div class=\"summary-card\"><div class=\"num\">"
        << result.only_right_count
        << "</div><div class=\"label\">" << escape_html(right_label) << "-only</div></div>\n";
    out << "</div>\n\n";

    // Changed files section
    if (result.changed_count > 0) {
        out << "<h2>Changed Files (" << escape_html(left_label) << " → "
            << escape_html(right_label) << ")</h2>\n";
        out << "<p style=\"color:var(--muted);margin-bottom:10px\">These "
            << result.changed_count
            << " files have <strong style=\"color:#e07070\">different hashes</strong>"
            << " between the releases:</p>\n";
        out << "<table>\n<thead>\n  <tr><th>#</th><th>File</th><th>Size</th>"
            << "<th>" << escape_html(left_label) << " Hash</th>"
            << "<th>" << escape_html(right_label) << " Hash</th></tr>\n</thead>\n<tbody>\n";

        int idx = 1;
        for (const auto& e : result.entries) {
            if (e.status != DiffStatus::Changed) continue;
            const char* cls = file_type_css_class(e.type);
            out << "  <tr class=\"diff\">\n"
                << "    <td>" << idx++ << "</td>\n"
                << "    <td" << (cls[0] ? std::string(" class=\"") + cls + "\"" : "")
                << ">" << escape_html(e.path) << "</td>\n"
                << "    <td class=\"size\">" << format_number(e.size) << "</td>\n"
                << "    <td class=\"hash\">" << e.left_hash << "</td>\n"
                << "    <td class=\"hash\">" << e.right_hash << "</td>\n"
                << "  </tr>\n";
        }
        out << "</tbody>\n</table>\n\n";

        // Auto-generated notes
        out << "<h3>Notes on changed files</h3>\n<ul class=\"notes\">\n";
        for (const auto& e : result.entries) {
            if (e.status != DiffStatus::Changed) continue;
            out << "  <li><code>" << escape_html(e.path) << "</code> &mdash; "
                << changed_file_note(e) << "</li>\n";
        }
        out << "</ul>\n\n";
    }

    // Key File Types callouts
    out << "<h2>Key File Types</h2>\n";
    if (has_boot_bin) {
        out << "<div class=\"callout boot\">Boot files present: ";
        if (has_1st_read) out << "<code>1st_read.bin</code> ";
        if (has_ip_bin) out << "<code>ip.bin</code> ";
        if (has_winceos) out << "<code>*winceos.bin</code> ";
        out << "</div>\n";
    } else {
        out << "<div class=\"callout boot\">No <code>1st_read.bin</code> or <code>ip.bin</code>"
            << " present" << (has_winceos ? " (uses WinCE boot binary)" : "")
            << ".</div>\n";
    }
    if (has_afs) {
        out << "<div class=\"callout archive\"><code>.afs</code> archives present.</div>\n";
    } else {
        out << "<div class=\"callout archive\">No <code>.afs</code> archives"
            << (has_pod ? " &mdash; this game uses <code>.pod</code> archives instead." : ".")
            << "</div>\n";
    }
    if (has_adx) {
        out << "<div class=\"callout audio\"><code>.adx</code> audio files present.</div>\n";
    } else {
        out << "<div class=\"callout audio\">No <code>.adx</code> audio files"
            << (has_pcm ? " &mdash; audio uses <code>.pcm</code> format." : ".")
            << "</div>\n";
    }
    out << "\n";

    // Legend
    out << "<h2>Full File Listing</h2>\n\n"
        << "<div class=\"legend\">\n"
        << "  <div class=\"legend-item\"><span class=\"legend-swatch orange\"></span>"
        << " Boot / executable (<code>1st_read.bin</code>, <code>ip.bin</code>,"
        << " <code>.exe</code>)</div>\n"
        << "  <div class=\"legend-item\"><span class=\"legend-swatch blue\"></span>"
        << " Archives (<code>.afs</code>, <code>.pod</code>)</div>\n"
        << "  <div class=\"legend-item\"><span class=\"legend-swatch green\"></span>"
        << " Audio (<code>.adx</code>, <code>.pcm</code>)</div>\n"
        << "  <div class=\"legend-item\"><span class=\"legend-swatch diff\"></span>"
        << " DIFF &mdash; hash differs between releases</div>\n"
        << "</div>\n\n";

    // Full file listing table
    out << "<table>\n<thead>\n  <tr><th>#</th><th>File</th><th>Size</th>"
        << "<th>" << escape_html(left_label) << "</th>"
        << "<th>" << escape_html(right_label) << "</th><th>Status</th></tr>\n"
        << "</thead>\n<tbody>\n";

    int idx = 1;
    for (const auto& e : result.entries) {
        std::string tr_class;
        if (e.status == DiffStatus::Changed) tr_class = " class=\"diff\"";

        const char* cls = file_type_css_class(e.type);
        std::string td_file = cls[0]
            ? std::string(" class=\"") + cls + "\""
            : "";

        std::string right_col;
        if (e.status == DiffStatus::OnlyLeft) {
            right_col = "<span class=\"dash\">&mdash;</span>";
        } else if (e.status == DiffStatus::Identical) {
            right_col = "same";
        } else {
            right_col = e.right_hash;
        }

        out << "  <tr" << tr_class << ">\n"
            << "    <td>" << idx++ << "</td>\n"
            << "    <td" << td_file << ">" << escape_html(e.path) << "</td>\n"
            << "    <td class=\"size\">" << format_number(e.size) << "</td>\n"
            << "    <td class=\"hash\">"
            << (e.left_hash.empty() ? "<span class=\"dash\">&mdash;</span>" : e.left_hash)
            << "</td>\n"
            << "    <td class=\"hash\">" << right_col << "</td>\n"
            << "    <td>" << status_badge(e.status) << "</td>\n"
            << "  </tr>\n";
    }
    out << "</tbody>\n</table>\n\n";

    out << "</div>\n</body>\n</html>\n";
}

// ---------------------------------------------------------------------------
// Markdown Generator
// ---------------------------------------------------------------------------

static void generate_markdown(const DiffResult& result, const std::string& filename,
                              const std::string& title, const std::string& left_label,
                              const std::string& right_label) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Error: cannot write to: " << filename << std::endl;
        std::exit(1);
    }

    // Scan for key file types
    bool has_1st_read = false, has_ip_bin = false, has_winceos = false;
    bool has_afs = false, has_pod = false;
    bool has_adx = false, has_pcm = false;
    for (const auto& e : result.entries) {
        std::string fn = to_lower(extract_filename(e.path));
        if (fn == "1st_read.bin") has_1st_read = true;
        if (fn == "ip.bin") has_ip_bin = true;
        if (ends_with(fn, "winceos.bin")) has_winceos = true;
        if (ends_with(fn, ".afs")) has_afs = true;
        if (ends_with(fn, ".pod")) has_pod = true;
        if (ends_with(fn, ".adx")) has_adx = true;
        if (ends_with(fn, ".pcm")) has_pcm = true;
    }
    bool has_boot_bin = has_1st_read || has_ip_bin || has_winceos;

    std::string size_note = (result.left_total_size == result.right_total_size)
        ? "identical" : "differs";
    std::string size_info = format_number(result.left_total_size) + " bytes";
    if (result.left_total_size != result.right_total_size) {
        size_info += " (Kudos) vs " + format_number(result.right_total_size) + " bytes (Vector)";
    }

    // Title
    out << "# " << escape_html(title) << "\n\n"
        << "**Total size:** " << size_info << " (" << size_note << ")\n\n";

    // Summary
    out << "## Summary\n\n"
        << "| Status | Count |\n|--------|-------|\n"
        << "| Identical files | " << result.identical_count << " |\n"
        << "| Changed files | **" << result.changed_count << "** |\n"
        << "| " << escape_html(left_label) << "-only files | "
        << result.only_left_count << " |\n"
        << "| " << escape_html(right_label) << "-only files | "
        << result.only_right_count << " |\n\n";

    // Changed files section
    if (result.changed_count > 0) {
        out << "## Changed Files (" << escape_html(left_label)
            << " → " << escape_html(right_label) << ")\n\n"
            << "These " << result.changed_count
            << " files have **different hashes** between the releases:\n\n"
            << "| # | File | Size | " << escape_html(left_label) << " Hash | "
            << escape_html(right_label) << " Hash |\n"
            << "|---|------|------|------------|-------------|\n";

        int idx = 1;
        for (const auto& e : result.entries) {
            if (e.status != DiffStatus::Changed) continue;
            const char* color = file_type_color(e.type);
            std::string file_cell;
            if (color[0]) {
                file_cell = "<span style=\"color:" + std::string(color) + "\">`"
                          + escape_html(e.path) + "`</span>";
            } else {
                file_cell = "`" + escape_html(e.path) + "`";
            }
            out << "| " << idx++ << " | " << file_cell
                << " | " << format_number(e.size)
                << " | `" << e.left_hash << "`"
                << " | `" << e.right_hash << "` |\n";
        }
        out << "\n### Notes on changed files\n\n";
        for (const auto& e : result.entries) {
            if (e.status != DiffStatus::Changed) continue;
            out << "- **`" << escape_html(e.path) << "`** — "
                << changed_file_note(e) << "\n";
        }
        out << "\n";
    }

    // Key File Types
    out << "## Key File Types\n\n";
    if (has_boot_bin) {
        out << "<span style=\"color:orange\">Boot files present: ";
        if (has_1st_read) out << "`1st_read.bin` ";
        if (has_ip_bin) out << "`ip.bin` ";
        if (has_winceos) out << "`*winceos.bin` ";
        out << "</span>\n\n";
    } else {
        out << "<span style=\"color:orange\">**No `1st_read.bin` or `ip.bin`"
            << " present**" << (has_winceos ? " — this game uses WinCE boot binary instead." : "")
            << "</span>\n\n";
    }
    if (!has_afs) {
        out << "<span style=\"color:dodgerblue\">**No `.afs` archives**"
            << (has_pod ? " — this game uses `.pod` archives instead." : "")
            << "</span>\n\n";
    }
    if (!has_adx) {
        out << "<span style=\"color:mediumseagreen\">**No `.adx` audio files**"
            << (has_pcm ? " — audio uses `.pcm` format." : "")
            << "</span>\n\n";
    }

    // Legend
    out << "## Full File Listing\n\nLegend:\n"
        << "- <span style=\"color:orange\">**Orange**</span>"
        << " = boot/executable files (`1st_read.bin`, `ip.bin`, `.exe`)\n"
        << "- <span style=\"color:dodgerblue\">**Blue**</span>"
        << " = archive files (`.afs`, `.pod`)\n"
        << "- <span style=\"color:mediumseagreen\">**Green**</span>"
        << " = audio files (`.adx`, `.pcm`)\n"
        << "- 🔴 **DIFF** = hash differs between releases\n\n";

    // Full file listing
    out << "| # | File | Size | " << escape_html(left_label)
        << " | " << escape_html(right_label) << " | Status |\n"
        << "|---|------|------|-------|--------|--------|\n";

    int idx = 1;
    for (const auto& e : result.entries) {
        const char* color = file_type_color(e.type);
        std::string file_cell;
        if (color[0]) {
            file_cell = "<span style=\"color:" + std::string(color) + "\">`"
                      + escape_html(e.path) + "`</span>";
        } else {
            file_cell = "`" + escape_html(e.path) + "`";
        }

        std::string right_col;
        if (e.status == DiffStatus::OnlyLeft) {
            right_col = "—";
        } else if (e.status == DiffStatus::Identical) {
            right_col = "same";
        } else {
            right_col = "`" + e.right_hash + "`";
        }

        std::string left_col = e.left_hash.empty()
            ? "—" : ("`" + e.left_hash + "`");

        std::string status_cell;
        switch (e.status) {
            case DiffStatus::Identical: status_cell = "✓"; break;
            case DiffStatus::Changed:   status_cell = "🔴 **DIFF**"; break;
            case DiffStatus::OnlyLeft:  status_cell = "⚠️ Only in " + left_label; break;
            case DiffStatus::OnlyRight: status_cell = "⚠️ Only in " + right_label; break;
        }

        out << "| " << idx++ << " | " << file_cell
            << " | " << format_number(e.size)
            << " | " << left_col
            << " | " << right_col
            << " | " << status_cell << " |\n";
    }
    out << "\n";
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

static void print_help(const char* prog) {
    std::cout <<
        "Usage: " << prog << " <file1> <file2> [options]\n"
        "\n"
        "Compare two Dreamcast database files and generate diff reports.\n"
        "\n"
        "Options:\n"
        "  --html <file>    Output HTML report (default: difference.html)\n"
        "  --md <file>      Output Markdown report\n"
        "  --title <text>   Custom report title\n"
        "  --left <label>   Label for first file (default: file1 name)\n"
        "  --right <label>  Label for second file (default: file2 name)\n"
        "  -h, --help       Show this help message\n"
        "\n"
        "Examples:\n"
        "  " << prog << " kudos.db vector.db\n"
        "  " << prog << " kudos.db vector.db --html report.html --md report.md\n"
        "  " << prog << " kudos.db vector.db --title \"My Game — K vs V\"\n"
        "\n";
}

// ---------------------------------------------------------------------------
// Title auto-generation
// ---------------------------------------------------------------------------

static std::string extract_stem(const std::string& path) {
    auto slash = path.find_last_of("\\/");
    std::string fname = (slash == std::string::npos) ? path : path.substr(slash + 1);
    // Remove common suffixes like " [!]"
    auto dot = fname.find('.');
    if (dot != std::string::npos && dot > 0) {
        // Only strip if it looks like an extension (short, alphanumeric)
        std::string ext = fname.substr(dot + 1);
        if (ext.size() <= 5) fname = fname.substr(0, dot);
    }
    return fname;
}

static std::string auto_title(const std::string& f1, const std::string& /*f2*/,
                              const std::string& left_label,
                              const std::string& right_label) {
    return extract_stem(f1) + " — " + left_label + " vs " + right_label + " Diff";
}

// ---------------------------------------------------------------------------
// Browser
// ---------------------------------------------------------------------------

static void open_in_browser(const std::string& filepath) {
#ifdef _WIN32
    std::string cmd = "start \"\" \"" + filepath + "\"";
#elif __APPLE__
    std::string cmd = "open \"" + filepath + "\"";
#else
    std::string cmd = "xdg-open \"" + filepath + "\"";
#endif
    std::system(cmd.c_str());
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Options opt;

    // Parse arguments
    bool explicit_html = false;
    bool explicit_md = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            opt.show_help = true;
        } else if (arg == "--html") {
            if (++i >= argc) {
                std::cerr << "Error: --html requires a filename argument." << std::endl;
                return 1;
            }
            opt.html_output = argv[i];
            explicit_html = true;
        } else if (arg == "--md") {
            if (++i >= argc) {
                std::cerr << "Error: --md requires a filename argument." << std::endl;
                return 1;
            }
            opt.md_output = argv[i];
        } else if (arg == "--title") {
            if (++i >= argc) {
                std::cerr << "Error: --title requires a text argument." << std::endl;
                return 1;
            }
            opt.title = argv[i];
        } else if (arg == "--left") {
            if (++i >= argc) {
                std::cerr << "Error: --left requires a label argument." << std::endl;
                return 1;
            }
            opt.left_label = argv[i];
        } else if (arg == "--right") {
            if (++i >= argc) {
                std::cerr << "Error: --right requires a label argument." << std::endl;
                return 1;
            }
            opt.right_label = argv[i];
        } else if (opt.file1.empty()) {
            opt.file1 = arg;
        } else if (opt.file2.empty()) {
            opt.file2 = arg;
        } else {
            std::cerr << "Error: unexpected argument: " << arg << std::endl;
            return 1;
        }
    }

    if (opt.show_help) {
        print_help(argv[0]);
        return 0;
    }

    if (opt.file1.empty() || opt.file2.empty()) {
        std::cerr << "Error: two input files required." << std::endl;
        print_help(argv[0]);
        return 1;
    }

    // Derive labels (use --left/--right if provided)
    std::string left_label  = opt.left_label.empty()
        ? extract_stem(opt.file1) : opt.left_label;
    std::string right_label = opt.right_label.empty()
        ? extract_stem(opt.file2) : opt.right_label;

    // Auto title if not provided
    if (opt.title.empty()) {
        opt.title = auto_title(opt.file1, opt.file2, left_label, right_label);
    }

    // Parse
    uint64_t left_size = 0, right_size = 0;
    auto left_entries  = parse_database(opt.file1, left_size);
    auto right_entries = parse_database(opt.file2, right_size);

    // Compare
    auto result = compare_databases(left_entries, right_entries,
                                    left_size, right_size);

    // Generate reports
    // Always generate HTML (with default or explicit path)
    generate_html(result, opt.html_output, opt.title, left_label, right_label);
    std::cout << "HTML report written to: " << opt.html_output
              << " (" << result.entries.size() << " files, "
              << result.changed_count << " changed)" << std::endl;

    if (explicit_md) {
        generate_markdown(result, opt.md_output, opt.title, left_label, right_label);
        std::cout << "Markdown report written to: " << opt.md_output << std::endl;
    }

    // Auto-open in browser when invoked with just 2 files (no explicit output options)
    if (!explicit_html && !explicit_md) {
        open_in_browser(opt.html_output);
    }

    return 0;
}
