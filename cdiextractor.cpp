#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#include <sys/utime.h>
#include <sys/stat.h>
#include <windows.h>
#define fseeko _fseeki64
#define ftello _ftelli64
static int make_dir(const char *path) { return _mkdir(path); }
static int set_mtime(const char *path, time_t t) {
    struct _utimbuf ut = { t, t };
    return _utime(path, &ut);
}
#else
#include <sys/stat.h>
#include <utime.h>
#include <dirent.h>
static int make_dir(const char *path) { return mkdir(path, 0755); }
static int set_mtime(const char *path, time_t t) {
    struct utimbuf ut = { t, t };
    return utime(path, &ut);
}
#endif

// CDI version constants
#define CDI_V2  0x80000004
#define CDI_V3  0x80000005
#define CDI_V35 0x80000006

struct CDITrack {
    uint32_t mode;
    uint32_t sector_size;
    uint32_t sector_size_value;
    int64_t  length;
    int64_t  pregap_length;
    int64_t  total_length;
    uint32_t start_lba;
    uint8_t  filename_length;
    int64_t  data_position;   // file offset where raw track data starts
};

struct CDIHeader {
    uint32_t version;
    uint32_t header_offset;
    int64_t  image_length;
    uint16_t sessions;
};

struct DirEntry {
    std::string name;
    uint32_t    extent_lba;   // absolute disc LBA
    uint32_t    data_length;
    uint8_t     flags;
    bool        is_dir;
    int64_t     timestamp;    // Unix time_t, -1 if not available
};

struct ScanEntry {
    std::string path;         // relative path with backslashes (Dreambeam convention)
    uint32_t    size;
    uint32_t    crc32;        // CRC32 of file contents
};

// Global scan list populated during extraction or scan mode
static std::vector<ScanEntry> g_scan_entries;
static uint64_t               g_scan_total_size = 0;

static uint32_t read32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read32le(FILE *f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return read32le(buf);
}

static void error_exit(const char *msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}

static void error_exit(const char *msg, const char *detail) {
    fprintf(stderr, "ERROR: %s: %s\n", msg, detail);
    exit(1);
}

// ============================================================
// CRC32 (zlib-compatible, used by Dreambeam)
// ============================================================

static uint32_t crc32_table[256];
static bool     crc32_table_ready = false;

static void crc32_init() {
    const uint32_t poly = 0xEDB88320u;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
        crc32_table[i] = crc;
    }
    crc32_table_ready = true;
}

// Incremental CRC32, mirrors zlib.crc32(data, prev)
static uint32_t crc32_update(uint32_t prev, const uint8_t *data, size_t len) {
    if (!crc32_table_ready) crc32_init();
    uint32_t crc = prev ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t crc32_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t crc = 0;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        crc = crc32_update(crc, buf, n);
    fclose(f);
    return crc;
}

// ============================================================
// CDI Image Parsing
// ============================================================

static void cdi_parse_header(FILE *f, CDIHeader &hdr) {
    fseeko(f, 0, SEEK_END);
    hdr.image_length = ftello(f);

    if (hdr.image_length < 8)
        error_exit("Image file is too short");

    fseeko(f, hdr.image_length - 8, SEEK_SET);
    hdr.version = read32le(f);
    hdr.header_offset = read32le(f);

    if (hdr.header_offset == 0)
        error_exit("Bad image format: header offset is zero");

    if (hdr.version != CDI_V2 && hdr.version != CDI_V3 && hdr.version != CDI_V35)
        error_exit("Unsupported CDI image version");

    if (hdr.version == CDI_V35)
        fseeko(f, hdr.image_length - hdr.header_offset, SEEK_SET);
    else
        fseeko(f, hdr.header_offset, SEEK_SET);

    hdr.sessions = 0;
    fread(&hdr.sessions, 2, 1, f);
    if (hdr.sessions == 0)
        error_exit("Bad format: Could not find session header");
}

static bool cdi_read_track(FILE *f, const CDIHeader &hdr, CDITrack &track) {
    uint8_t start_mark[10];
    uint8_t expected_mark[10] = { 0, 0, 0x01, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
    uint32_t temp;

    temp = read32le(f);
    if (temp != 0)
        fseeko(f, 8, SEEK_CUR);

    fread(start_mark, 1, 10, f);
    if (memcmp(expected_mark, start_mark, 10) != 0)
        return false;

    fread(start_mark, 1, 10, f);
    if (memcmp(expected_mark, start_mark, 10) != 0)
        return false;

    fseeko(f, 4, SEEK_CUR);
    fread(&track.filename_length, 1, 1, f);
    fseeko(f, track.filename_length, SEEK_CUR);
    fseeko(f, 11, SEEK_CUR);
    fseeko(f, 4, SEEK_CUR);
    fseeko(f, 4, SEEK_CUR);

    temp = read32le(f);
    if (temp == 0x80000000)
        fseeko(f, 8, SEEK_CUR);

    fseeko(f, 2, SEEK_CUR);

    uint32_t pregap_le, length_le, mode_le, start_lba_le, total_length_le, ssv_le;
    fread(&pregap_le, 4, 1, f);
    fread(&length_le, 4, 1, f);
    track.pregap_length = (int64_t)(int32_t)pregap_le;
    track.length = (int64_t)(int32_t)length_le;

    fseeko(f, 6, SEEK_CUR);

    fread(&mode_le, 4, 1, f);
    track.mode = mode_le;

    fseeko(f, 12, SEEK_CUR);

    fread(&start_lba_le, 4, 1, f);
    fread(&total_length_le, 4, 1, f);
    track.start_lba = start_lba_le;
    track.total_length = (int64_t)(int32_t)total_length_le;

    fseeko(f, 16, SEEK_CUR);

    fread(&ssv_le, 4, 1, f);
    track.sector_size_value = ssv_le;

    switch (track.sector_size_value) {
        case 0: track.sector_size = 2048; break;
        case 1: track.sector_size = 2336; break;
        case 2: track.sector_size = 2352; break;
        default: error_exit("Unsupported sector size");
    }

    if (track.mode > 2)
        error_exit("Unsupported track mode");

    fseeko(f, 29, SEEK_CUR);
    if (hdr.version != CDI_V2) {
        fseeko(f, 5, SEEK_CUR);
        temp = read32le(f);
        if (temp == 0xffffffff)
            fseeko(f, 78, SEEK_CUR);
    }

    return true;
}

// ============================================================
// ISO9660 Filesystem Parsing
// (extents are absolute disc LBAs, we convert using lba_offset)
// ============================================================

// Convert ISO extent (disc LBA) to local sector index.
// Returns -1 if the sector is outside our extracted data range.
// Parse ISO9660 7-byte date/time (offset 18 of directory record) to Unix time_t.
// Format: year-1900, month, day, hour, minute, second, gmtoff/15min
static int64_t iso_parse_date(const uint8_t *d) {
    int year   = d[0] + 1900;
    int month  = d[1];
    int day    = d[2];
    int hour   = d[3];
    int minute = d[4];
    int second = d[5];
    // d[6] = GMT offset in 15-min intervals, ignored for local time preservation

    if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
    if (hour > 23 || minute > 59 || second > 59) return -1;

    struct tm tm_buf = {};
    tm_buf.tm_year = year - 1900;
    tm_buf.tm_mon  = month - 1;
    tm_buf.tm_mday = day;
    tm_buf.tm_hour = hour;
    tm_buf.tm_min  = minute;
    tm_buf.tm_sec  = second;
    tm_buf.tm_isdst = -1;

    // mktime normalises the fields and returns local-time-based epoch
    time_t t = mktime(&tm_buf);
    return (t == (time_t)-1) ? -1 : (int64_t)t;
}

static int64_t extent_to_local(uint32_t extent, uint32_t lba_offset, uint32_t num_sectors) {
    if (extent < lba_offset) return -1;
    int64_t local = (int64_t)extent - lba_offset;
    if (local >= (int64_t)num_sectors) return -1;
    return local;
}

static bool iso_read_pvd(const std::vector<uint8_t> &sectors, uint32_t num_sectors,
                         uint32_t &root_extent, uint32_t &root_size) {
    for (int pvd_sector : {16, 0}) {
        if (pvd_sector >= (int)num_sectors) continue;

        const uint8_t *p = sectors.data() + pvd_sector * 2048;

        if (p[0] != 0x01) continue;
        if (memcmp(p + 1, "CD001", 5) != 0) continue;
        if (p[6] != 0x01) continue;

        const uint8_t *root = p + 156;
        uint8_t root_len = root[0];
        if (root_len < 34) continue;

        root_extent = read32le(root + 2);
        root_size   = read32le(root + 10);
        return true;
    }
    return false;
}

static void iso_collect_entries(const std::vector<uint8_t> &sectors, uint32_t num_sectors,
                                uint32_t lba_offset,
                                uint32_t extent, uint32_t size,
                                std::vector<DirEntry> &entries) {
    int64_t local_sector = extent_to_local(extent, lba_offset, num_sectors);
    if (local_sector < 0) {
        fprintf(stderr, "WARNING: directory extent %u out of range (lba_offset=%u, nsectors=%u)\n",
                extent, lba_offset, num_sectors);
        return;
    }

    uint32_t bytes_read = 0;
    uint32_t cur = (uint32_t)local_sector;

    while (bytes_read < size && cur < num_sectors) {
        const uint8_t *p = sectors.data() + cur * 2048;
        uint32_t offset = 0;

        while (offset < 2048 && bytes_read < size) {
            uint8_t rec_len = p[offset];
            if (rec_len == 0) {
                bytes_read = size; // end of directory
                break;
            }
            if (rec_len < 34 || offset + rec_len > 2048) break;

            const uint8_t *rec = p + offset;
            uint8_t file_flags = rec[25];
            uint32_t file_extent = read32le(rec + 2);
            uint32_t file_size   = read32le(rec + 10);
            uint8_t id_len = rec[32];

            // Skip . (id_len=1, name=0x00) and .. (id_len=1, name=0x01)
            if (id_len == 1 && (rec[33] == 0x00 || rec[33] == 0x01))
                goto next_entry;

            if (id_len > 0 && file_extent > 0) {
                DirEntry e;
                e.extent_lba  = file_extent;
                e.data_length = file_size;
                e.flags       = file_flags;
                e.is_dir      = (file_flags & 0x02) != 0;

                // Try Rock Ridge NM (Alternate Name) for the real POSIX filename.
                // System Use area starts after the ISO9660 name at rec+33+id_len.
                std::string rr_name;
                int su_off = 33 + id_len;
                int su_end = (int)rec_len;
                while (su_off + 4 <= su_end) {
                    // SUSP header: 2-byte signature, 1-byte len, 1-byte version
                    if (rec[su_off] == 'N' && rec[su_off + 1] == 'M') {
                        uint8_t entry_len  = rec[su_off + 2];
                        if (entry_len >= 5 && su_off + entry_len <= su_end) {
                            uint8_t flags = rec[su_off + 4];
                            // Skip . and .. references
                            if (!(flags & 0x06)) {
                                rr_name.assign((const char *)(rec + su_off + 5),
                                               entry_len - 5);
                            }
                        }
                    }
                    if (rec[su_off + 2] < 4) break; // invalid length
                    su_off += rec[su_off + 2];
                }

                std::string raw_name((const char *)(rec + 33), id_len);
                // Prefer Rock Ridge name if available, otherwise use ISO9660 name
                if (!rr_name.empty())
                    raw_name = rr_name;
                size_t semicol = raw_name.find(';');
                if (semicol != std::string::npos)
                    raw_name = raw_name.substr(0, semicol);

                // Rtrim spaces
                size_t last_nonspace = raw_name.find_last_not_of(' ');
                if (last_nonspace != std::string::npos)
                    raw_name = raw_name.substr(0, last_nonspace + 1);

                e.name = raw_name;
                e.timestamp = iso_parse_date(rec + 18);

                if (!e.name.empty())
                    entries.push_back(e);
            }

            next_entry:

            offset += rec_len;
            bytes_read += rec_len;
        }

        cur++;
    }
}

static void iso_list_recursive(const std::vector<uint8_t> &sectors, uint32_t num_sectors,
                               uint32_t lba_offset,
                               uint32_t extent, uint32_t size,
                               const std::string &prefix, bool show_all) {
    std::vector<DirEntry> entries;
    iso_collect_entries(sectors, num_sectors, lba_offset, extent, size, entries);

    for (const auto &e : entries) {
        if (e.is_dir) {
            if (show_all)
                printf("%s%s/\n", prefix.c_str(), e.name.c_str());
            iso_list_recursive(sectors, num_sectors, lba_offset,
                               e.extent_lba, e.data_length,
                               prefix + e.name + "/", show_all);
        } else {
            printf("%s%s  (%u bytes)\n", prefix.c_str(), e.name.c_str(), e.data_length);
        }
    }
}

// Process one file's data from sectors: write to disk (if out!=NULL) and/or
// compute CRC32 for scan mode.  Adds a ScanEntry when do_scan is true.
static void process_file_data(const std::vector<uint8_t> &sectors, uint32_t num_sectors,
                              const DirEntry &e, uint32_t lba_offset,
                              FILE *out, bool do_scan, const std::string &scan_path) {
    int64_t local_sector = extent_to_local(e.extent_lba, lba_offset, num_sectors);
    if (local_sector < 0) {
        fprintf(stderr, "WARNING: skipping '%s' (extent %u out of range)\n",
                scan_path.c_str(), e.extent_lba);
        return;
    }

    uint32_t remaining = e.data_length;
    uint32_t sector = (uint32_t)local_sector;
    uint32_t crc = 0;

    while (remaining > 0 && sector < num_sectors) {
        uint32_t chunk = remaining < 2048 ? remaining : 2048;
        const uint8_t *src = sectors.data() + sector * 2048;

        if (out)
            fwrite(src, 1, chunk, out);
        if (do_scan)
            crc = crc32_update(crc, src, chunk);

        remaining -= chunk;
        sector++;
    }

    if (do_scan) {
        // Dreambeam uses backslashes in paths
        std::string db_path = scan_path;
        std::replace(db_path.begin(), db_path.end(), '/', '\\');
        ScanEntry se;
        se.path = db_path;
        se.size = e.data_length;
        se.crc32 = crc;
        g_scan_entries.push_back(se);
        g_scan_total_size += e.data_length;
    }
}

static void iso_extract_recursive(const std::vector<uint8_t> &sectors, uint32_t num_sectors,
                                  uint32_t lba_offset,
                                  uint32_t extent, uint32_t size,
                                  const std::string &dest_dir, const std::string &prefix,
                                  bool do_scan) {
    std::vector<DirEntry> entries;
    iso_collect_entries(sectors, num_sectors, lba_offset, extent, size, entries);

    for (const auto &e : entries) {
        if (e.is_dir) {
            std::string dir_path = dest_dir + "/" + prefix + e.name;
            make_dir(dir_path.c_str());
            iso_extract_recursive(sectors, num_sectors, lba_offset,
                                  e.extent_lba, e.data_length,
                                  dest_dir, prefix + e.name + "/", do_scan);
            if (e.timestamp >= 0)
                set_mtime(dir_path.c_str(), (time_t)e.timestamp);
        } else {
            std::string rel_path = prefix + e.name;
            std::string file_path = dest_dir + "/" + rel_path;
            FILE *out = fopen(file_path.c_str(), "wb");
            if (!out) {
                fprintf(stderr, "ERROR: Cannot create file '%s'\n", file_path.c_str());
                continue;
            }

            process_file_data(sectors, num_sectors, e, lba_offset,
                              out, do_scan, rel_path);

            fclose(out);
            if (e.timestamp >= 0)
                set_mtime(file_path.c_str(), (time_t)e.timestamp);

            printf("Extracted: %s%s\n", prefix.c_str(), e.name.c_str());
        }
    }
}

// Scan-only recursive walk (no disk writes, just CRC32 collection)
static void iso_scan_recursive(const std::vector<uint8_t> &sectors, uint32_t num_sectors,
                               uint32_t lba_offset,
                               uint32_t extent, uint32_t size,
                               const std::string &prefix) {
    std::vector<DirEntry> entries;
    iso_collect_entries(sectors, num_sectors, lba_offset, extent, size, entries);

    for (const auto &e : entries) {
        if (e.is_dir) {
            iso_scan_recursive(sectors, num_sectors, lba_offset,
                               e.extent_lba, e.data_length,
                               prefix + e.name + "/");
        } else {
            process_file_data(sectors, num_sectors, e, lba_offset,
                              nullptr, true, prefix + e.name);
        }
    }
}

// ============================================================
// Directory scanning (Dreambeam mode on a folder)
// ============================================================

// Returns true if the filename should be excluded from scans
static bool is_scan_excluded(const std::string &name, bool exclude_boot) {
    // Skip our own _dbscan.txt output files
    size_t len = name.size();
    if (len > 11) {
        std::string suffix = name.substr(len - 11);
        if (suffix == "_dbscan.txt") return true;
    }
    // Optionally exclude bootsector.bin (included by default for VGA/region identification)
    if (exclude_boot && name == "bootsector.bin") return true;
    return false;
}

// Walk a directory recursively, computing CRC32 for every file.
// base_path is stripped from the start of each path to get relative paths.
static void scan_directory_files(const std::string &dir_path, const std::string &base_path) {
#ifdef _WIN32
    std::string search = dir_path + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string full = dir_path + "\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory_files(full, base_path);
            continue;
        }

        if (is_scan_excluded(name, false)) continue;

        // Compute relative path from base_path
        std::string rel = full.substr(base_path.size());
        if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel = rel.substr(1);

        uint32_t crc = crc32_file(full.c_str());
        // Get file size
        HANDLE fh = CreateFileA(full.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        uint32_t fsize = 0;
        if (fh != INVALID_HANDLE_VALUE) {
            fsize = GetFileSize(fh, NULL);
            CloseHandle(fh);
        }

        // Dreambeam convention: backslashes in paths
        std::replace(rel.begin(), rel.end(), '/', '\\');
        ScanEntry se;
        se.path = rel;
        se.size = fsize;
        se.crc32 = crc;
        g_scan_entries.push_back(se);
        g_scan_total_size += fsize;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir_path.c_str());
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir_path + "/" + name;

        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_directory_files(full, base_path);
            continue;
        }

        if (is_scan_excluded(name, false)) continue;

        // Compute relative path from base_path
        std::string rel = full.substr(base_path.size());
        if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);

        uint32_t crc = crc32_file(full.c_str());
        uint32_t fsize = (uint32_t)st.st_size;

        // Dreambeam convention: backslashes
        std::replace(rel.begin(), rel.end(), '/', '\\');
        ScanEntry se;
        se.path = rel;
        se.size = fsize;
        se.crc32 = crc;
        g_scan_entries.push_back(se);
        g_scan_total_size += fsize;
    }
    closedir(d);
#endif
}

// ============================================================
// Dreambeam scan list output
// ============================================================

static void write_dreambeam_list(const char *path) {
    // Sort entries by path for consistent hashing regardless of extraction order
    std::sort(g_scan_entries.begin(), g_scan_entries.end(),
              [](const ScanEntry &a, const ScanEntry &b) { return a.path < b.path; });

    FILE *f = fopen(path, "wb");  // binary to avoid \n→\r\n translation on Windows
    if (!f) {
        fprintf(stderr, "ERROR: Cannot write scan list '%s'\n", path);
        return;
    }
    fprintf(f, "Total size: %llu bytes.\r\n", (unsigned long long)g_scan_total_size);
    for (const auto &se : g_scan_entries)
        fprintf(f, "%s [%u bytes] - %08X\r\n", se.path.c_str(), se.size, se.crc32);
    fclose(f);

    // Compute game hash (CRC32 of the whole listing)
    uint32_t game_crc = crc32_file(path);
    printf("\nDreambeam scan list: %s\n", path);
    printf("  %zu files, %llu total bytes\n",
           g_scan_entries.size(), (unsigned long long)g_scan_total_size);
    printf("  Game hash: %08X\n", game_crc);
}

// Try to match the game hash against a games.dat database file.
static void match_database(const char *scan_path, const char *db_dir) {
    uint32_t game_crc = crc32_file(scan_path);
    if (game_crc == 0) return;

    std::string games_dat = std::string(db_dir) + "/games.dat";
    // Also try Base/games.dat subdirectory convention
    FILE *db = fopen(games_dat.c_str(), "r");
    if (!db) {
        games_dat = std::string(db_dir) + "/Base/games.dat";
        db = fopen(games_dat.c_str(), "r");
    }
    if (!db) {
        printf("  (no games.dat found in %s)\n", db_dir);
        return;
    }

    char line[4096];
    bool found = false;
    while (fgets(line, sizeof(line), db)) {
        // Format: "Game Title (Region) (Group) [!] - CRC32HEX"
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
            line[--len] = '\0';
        if (len < 11) continue;

        // Last 8 chars before newline = CRC32, preceded by " - "
        std::string crc_str(line + len - 8, 8);
        uint32_t db_crc = 0;
        sscanf(crc_str.c_str(), "%x", &db_crc);

        if (db_crc == game_crc) {
            // Title is everything before " - XXXXXXXX"
            std::string title(line, len - 11); // strip " - XXXXXXXX"
            printf("  MATCH: %s\n", title.c_str());
            found = true;
            // Don't break — there may be multiple matches (alt versions)
        }
    }
    fclose(db);

    if (!found)
        printf("  No match found in database.\n");
}

// ============================================================
// Main
// ============================================================

static void print_usage(const char *prog) {
    printf("CDI Extractor - Extract files from DiscJuggler CDI images\n");
    printf("Usage: %s <image.cdi> [options]\n\n", prog);
    printf("Options:\n");
    printf("  -l, --list        List files inside the CDI image\n");
    printf("  -x, --extract     Extract all files (to current directory or -C dir)\n");
    printf("  -b, --bootsector  Also extract the boot sector (IP.BIN) as bootsector.bin\n");
    printf("  -s, --scan        Generate Dreambeam-compatible scan list (_dbscan.txt)\n");
    printf("  -m, --match <db>  Match scan against Dreambeam database at <db>\n");
    printf("  -C <dir>          Output directory for extraction\n");
    printf("  -a, --all         Include directories when listing\n");
    printf("  -h, --help        Show this help\n");
}

// Print IP.BIN info from a 2048-byte boot sector
static void print_ipbin_info(const uint8_t *sector) {
    // Verify magic: "SEGA SEGAKATANA " or "SEGA SEGASATAKA " at offset 0 (16 bytes with trailing space)
    if (memcmp(sector, "SEGA SEGAKATANA ", 16) != 0 &&
        memcmp(sector, "SEGA SEGASATAKA ", 16) != 0) {
        printf("  (no valid IP.BIN signature found)\n");
        return;
    }

    auto trim16_r = [](const uint8_t *p) -> std::string {
        std::string s((const char *)p, 16);
        size_t pos = s.find_last_not_of(' ');
        if (pos != std::string::npos) s.resize(pos + 1);
        return s;
    };

    printf("  Hardware ID : %s\n", trim16_r(sector + 0x00).c_str());
    printf("  Maker ID    : %s\n", trim16_r(sector + 0x10).c_str());
    printf("  Device Info : %s\n", trim16_r(sector + 0x20).c_str());
    printf("  Area/Product: %s\n", trim16_r(sector + 0x30).c_str());
    printf("  Version     : %s\n", trim16_r(sector + 0x40).c_str());
    printf("  Date        : %s\n", trim16_r(sector + 0x50).c_str());
    printf("  Boot file   : %s\n", trim16_r(sector + 0x60).c_str());

    // Title at offset 0x80 (varies by IP.BIN format, often 128 bytes)
    // Standard IP.BIN title area:
    std::string title((const char *)(sector + 0x80), 128);
    size_t tpos = title.find_last_not_of(' ');
    if (tpos != std::string::npos) title.resize(tpos + 1);
    if (!title.empty())
        printf("  Title       : %s\n", title.c_str());
}

int main(int argc, char **argv) {
    const char *image_path = nullptr;
    const char *output_dir = ".";
    bool do_list = false;
    bool do_extract = false;
    bool do_bootsector = false;
    bool do_scan = false;
    const char *db_path = nullptr;
    bool list_all = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            do_list = true;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--extract") == 0) {
            do_extract = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bootsector") == 0) {
            do_bootsector = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scan") == 0) {
            do_scan = true;
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--match") == 0) && i + 1 < argc) {
            db_path = argv[++i];
            do_scan = true; // match implies scan
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            list_all = true;
        } else if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            image_path = argv[i];
        }
    }

    if (!image_path) {
        print_usage(argv[0]);
        return 1;
    }

    // Default behaviour (no flags): extract + list + bootsector + scan, output to data/ next to CDI
    bool dragdrop_mode = false;
    if (!do_list && !do_extract && !do_bootsector && !do_scan) {
        do_list = true;
        do_extract = true;
        do_bootsector = true;
        do_scan = true;
        dragdrop_mode = true;
    }

    // Check if input is a directory — if so, just scan it (no CDI parsing)
    struct stat path_stat;
    if (stat(image_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
        if (!do_list && !do_extract && !do_scan) {
            // No flags: default to scan mode for directories
            do_scan = true;
        }

        if (do_scan) {
            printf("CDI Extractor\n\n");
            printf("Scanning directory: %s\n", image_path);

            std::string base_path(image_path);
            // Ensure trailing separator for proper relative path calculation
            char last = base_path.empty() ? 0 : base_path.back();
            if (last != '/' && last != '\\') base_path += '/';

            g_scan_entries.clear();
            g_scan_total_size = 0;
            scan_directory_files(base_path, base_path);

            printf("  %zu files, %llu total bytes\n",
                   g_scan_entries.size(), (unsigned long long)g_scan_total_size);

            // Write scan file next to the directory
            // Strip trailing separator for filename
            std::string clean_path = base_path;
            while (!clean_path.empty() &&
                   (clean_path.back() == '/' || clean_path.back() == '\\'))
                clean_path.pop_back();
            size_t slash = clean_path.find_last_of("/\\");
            std::string name = (slash != std::string::npos)
                ? clean_path.substr(slash + 1) : clean_path;
            std::string parent = (slash != std::string::npos)
                ? clean_path.substr(0, slash + 1) : "";
            std::string scan_path = parent + name + "_dbscan.txt";
            write_dreambeam_list(scan_path.c_str());

            if (db_path)
                match_database(scan_path.c_str(), db_path);
        }
        return 0;
    }

    // Drag-and-drop mode: output to {cdi_dir}/data/
    std::string computed_output_dir;
    if (dragdrop_mode) {
        std::string impath(image_path);
        size_t slash = impath.find_last_of("/\\");
        if (slash != std::string::npos)
            computed_output_dir = impath.substr(0, slash) + "/data";
        else
            computed_output_dir = "data";
        output_dir = computed_output_dir.c_str();
    }

    FILE *f = fopen(image_path, "rb");
    if (!f) {
        std::string alt_path = std::string(image_path) + ".cdi";
        f = fopen(alt_path.c_str(), "rb");
        if (!f) error_exit("Cannot open image file", image_path);
    }

    printf("CDI Extractor\n\n");
    printf("Opening: %s\n", image_path);

    // Parse CDI header
    CDIHeader hdr = {};
    cdi_parse_header(f, hdr);

    printf("CDI version: %s\n",
           hdr.version == CDI_V2 ? "2.0" :
           hdr.version == CDI_V3 ? "3.0" : "3.5");
    printf("Sessions: %u\n", hdr.sessions);

    // First pass: collect all track info (track data starts at byte 0)
    uint16_t remaining_sessions = hdr.sessions;
    int session_num = 0;
    int total_track_num = 0;
    std::vector<CDITrack> data_tracks;
    int64_t current_data_offset = 0;

    while (remaining_sessions > 0) {
        session_num++;

        uint16_t tracks;
        fread(&tracks, 2, 1, f);
        printf("\nSession %d: %d track(s)\n", session_num, tracks);

        for (int t = 0; t < tracks; t++) {
            total_track_num++;
            CDITrack track = {};
            if (!cdi_read_track(f, hdr, track))
                error_exit("Failed to read track header");

            track.data_position = current_data_offset;

            const char *type_str;
            switch (track.mode) {
                case 0: type_str = "Audio"; break;
                case 1: type_str = "Mode1"; break;
                case 2: type_str = "Mode2"; break;
                default: type_str = "Unknown"; break;
            }

            printf("  Track %d: %s/%u  Sectors: %lld  LBA: %u\n",
                   total_track_num, type_str, track.sector_size,
                   (long long)track.length, track.start_lba);

            current_data_offset += track.total_length * track.sector_size;

            if (track.mode != 0 && track.length > 0)
                data_tracks.push_back(track);
        }

        fseeko(f, 4, SEEK_CUR);
        fseeko(f, 8, SEEK_CUR);
        if (hdr.version != CDI_V2) fseeko(f, 1, SEEK_CUR);

        remaining_sessions--;
    }

    if (data_tracks.empty())
        error_exit("No data tracks found in the CDI image");

    // Use the FIRST data track's LBA as offset (ISO9660 extents reference
    // from the first session). Extract ALL data tracks into one contiguous
    // sector array so that data/data images (e.g. LBA 0 + LBA 44618) work.
    uint32_t lba_offset = data_tracks[0].start_lba;

    std::vector<uint8_t> sectors;
    for (const auto &track : data_tracks) {
        int header_len = 0;
        if (track.mode == 2) {
            switch (track.sector_size) {
                case 2352: header_len = 24; break;
                case 2336: header_len = 8;  break;
                default:   header_len = 0;  break;
            }
        } else if (track.mode == 1) {
            switch (track.sector_size) {
                case 2352: header_len = 16; break;
                default:   header_len = 0;  break;
            }
        }

        int64_t ns = track.length;
        if (ns <= 0 || ns > 1000000) continue;

        uint32_t nsectors = (uint32_t)ns;
        size_t start_idx = sectors.size();
        sectors.resize(start_idx + nsectors * 2048);

        std::vector<uint8_t> raw_buf(track.sector_size);
        fseeko(f, track.data_position + track.pregap_length * track.sector_size, SEEK_SET);

        printf("\nExtracting track at LBA %u: %u sectors...\n",
               track.start_lba, nsectors);

        for (uint32_t i = 0; i < nsectors; i++) {
            size_t read_n = fread(raw_buf.data(), 1, track.sector_size, f);
            if (read_n < track.sector_size)
                break;

            uint8_t *dest = sectors.data() + start_idx + i * 2048;
            if (header_len > 0 && track.sector_size >= (uint32_t)(header_len + 2048)) {
                memcpy(dest, raw_buf.data() + header_len, 2048);
            } else if (track.sector_size == 2048) {
                memcpy(dest, raw_buf.data(), 2048);
            } else {
                size_t copy_n = std::min((size_t)2048, (size_t)track.sector_size);
                memcpy(dest, raw_buf.data(), copy_n);
                if (copy_n < 2048)
                    memset(dest + copy_n, 0, 2048 - copy_n);
            }
        }
    }

    fclose(f);

    uint32_t num_sectors = (uint32_t)(sectors.size() / 2048);
    printf("\nExtracted %u total ISO sectors (%zu bytes) from %zu track(s)\n",
           num_sectors, sectors.size(), data_tracks.size());

    // Ensure output directory exists before any output
    if (do_extract || do_scan || do_bootsector)
        make_dir(output_dir);

    // Boot sector (IP.BIN) — first 16 sectors of the first data track
    if (do_bootsector && num_sectors > 0) {
        uint32_t boot_sectors = (num_sectors >= 16) ? 16 : num_sectors;
        uint32_t boot_bytes  = boot_sectors * 2048;
        printf("\nBoot sector (first %u sector%s of first data track):\n",
               boot_sectors, boot_sectors > 1 ? "s" : "");
        print_ipbin_info(sectors.data());

        if (do_extract) {
            std::string boot_path = std::string(output_dir) + "/bootsector.bin";
            FILE *bf = fopen(boot_path.c_str(), "wb");
            if (bf) {
                fwrite(sectors.data(), 1, boot_bytes, bf);
                fclose(bf);
                printf("  -> saved %u bytes to bootsector.bin\n", boot_bytes);
            } else {
                fprintf(stderr, "WARNING: Could not write bootsector.bin\n");
            }
        }

        // Include bootsector in the scan so directory scans and CDI scans match
        if (do_scan) {
            uint32_t boot_crc = crc32_update(0, sectors.data(), boot_bytes);
            ScanEntry se;
            se.path = "bootsector.bin";
            se.size = boot_bytes;
            se.crc32 = boot_crc;
            g_scan_entries.push_back(se);
            g_scan_total_size += boot_bytes;
        }
    }

    // Parse ISO9660 filesystem
    uint32_t root_extent = 0, root_size = 0;
    if (!iso_read_pvd(sectors, num_sectors, root_extent, root_size))
        error_exit("Could not find ISO9660 Primary Volume Descriptor");

    printf("ISO9660 root: extent=%u, size=%u (local=%lld)\n",
           root_extent, root_size, (long long)extent_to_local(root_extent, lba_offset, num_sectors));

    if (do_list) {
        printf("\nFiles in image:\n");
        printf("----------------------------------------------\n");
        iso_list_recursive(sectors, num_sectors, lba_offset,
                           root_extent, root_size, "", list_all);
    }

    // Scan mode: walk filesystem computing CRC32s (without writing files)
    if (do_scan && !do_extract) {
        iso_scan_recursive(sectors, num_sectors, lba_offset,
                           root_extent, root_size, "");
    }

    if (do_extract) {
        printf("\nExtracting to: %s\n", output_dir);
        printf("----------------------------------------------\n");
        iso_extract_recursive(sectors, num_sectors, lba_offset,
                              root_extent, root_size, output_dir, "", do_scan);
        printf("\nExtraction complete.\n");
    }

    // Write Dreambeam scan list (next to the CDI, not in output dir)
    if (do_scan) {
        std::string impath(image_path);
        size_t dot  = impath.find_last_of('.');
        size_t slash = impath.find_last_of("/\\");
        std::string dir = (slash != std::string::npos) ? impath.substr(0, slash + 1) : "";
        std::string base = (slash != std::string::npos)
            ? impath.substr(slash + 1, (dot != std::string::npos ? dot : impath.size()) - slash - 1)
            : impath.substr(0, (dot != std::string::npos ? dot : impath.size()));
        std::string scan_path = dir + base + "_dbscan.txt";
        write_dreambeam_list(scan_path.c_str());

        if (db_path)
            match_database(scan_path.c_str(), db_path);
    }

    return 0;
}
