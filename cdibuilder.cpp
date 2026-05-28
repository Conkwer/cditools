#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

#define CDI_V35 0x80000006

static void error_exit(const char *msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}

static void w32le(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

// ============================================================
// CDI track header builder (replaces manual byte template)
// Builds a 244-byte track header matching what cdi_read_track expects.
// ============================================================

static void wb(std::vector<uint8_t> &b, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    b.insert(b.end(), p, p + len);
}

static void w8(std::vector<uint8_t> &b, uint8_t v)  { b.push_back(v); }
static void w32(std::vector<uint8_t> &b, uint32_t v) { uint8_t t[4]; w32le(t, v); wb(b, t, 4); }
static void wi32(std::vector<uint8_t> &b, int32_t v)  { w32(b, (uint32_t)v); }
static void wz(std::vector<uint8_t> &b, int n) { while (n-- > 0) w8(b, 0); }

// ============================================================
// Golden header blobs (extracted from real DiscJuggler v3.5 CDs).
// These are patched with dynamic values at runtime.
// The approach mirrors mkcdi.py — using pre-built templates
// preserves every magic byte that Iso7z validates.
// ============================================================

// Patch a uint32 LE at the given offset
static void patch32(uint8_t *buf, int off, uint32_t val) {
    buf[off] = val & 0xFF;
    buf[off+1] = (val >> 8) & 0xFF;
    buf[off+2] = (val >> 16) & 0xFF;
    buf[off+3] = (val >> 24) & 0xFF;
}

// Audio/data template (665 bytes, from test.cdi).
// Offsets of dynamic fields:
//   Track 1 (audio): pregap_offset=74, length_offset=78, total_offset=108
//     (all within track1 hdr starting at tpl offset 4; the +4 is already in the offset)
//   Track 2 (data):  pregap_offset=333, length_offset=337, lba_offset=367,
//     total_offset=371, total_alt=387, sectors_valve=632 (patched later)
//   Volume name at end: offset 594 (32 bytes padded with spaces)
static uint8_t *build_header_audio(int s1_pregap, int s1_length, int s2_pregap,
                                    int s2_length, int s2_lba, const char *vol) {
        static const uint8_t tpl[665] = {
        0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xAB, 0x00, 0x10, 0x02,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x40, 0x7E, 0x05, 0x00, 0x00, 0x00, 0x98, 0x00, 0x02, 0x00, 0x96, 0x00, 0x00, 0x00, 0x2E, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC4, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x80, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x44, 0xAC, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xAB,
        0x00, 0x10, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x80, 0x40, 0x7E, 0x05, 0x00, 0x00, 0x00, 0x98, 0x00, 0x02, 0x00, 0x96, 0x00, 0x00,
        0x00, 0x4F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB6, 0x2D, 0x00, 0x00, 0xE5,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0xE5, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x44, 0xAC, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xB6, 0x2D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFF, 0xAB, 0x00, 0x10, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x40, 0x7E, 0x05, 0x00, 0x00, 0x00, 0x98, 0x00, 0xA0, 0x34,
        0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    uint8_t *buf = (uint8_t*)malloc(665);
    memcpy(buf, tpl, 665);

    // Track 1 (audio, starts at blob offset 4):
    //   pregap=74, length=78, total=108  (lba=104 stays 0)
    patch32(buf, 74, (uint32_t)s1_pregap);
    patch32(buf, 78, (uint32_t)s1_length);
    patch32(buf, 108, (uint32_t)(s1_pregap + s1_length));

    // Track 2 (data, starts at blob offset 263):
    //   pregap=333, length=337, lba=363, total=367, ssv=387
    patch32(buf, 333, (uint32_t)s2_pregap);
    patch32(buf, 337, (uint32_t)s2_length);
    patch32(buf, 363, (uint32_t)s2_lba);
    patch32(buf, 367, (uint32_t)(s2_pregap + s2_length));
    // ssv at 387 stays 1 (2336 bytes/sector) — don't touch it

    // Volume name at offset 594: 32 bytes, space-padded
    memset(buf + 594, ' ', 32);
    size_t vl = strlen(vol);
    if (vl > 32) vl = 32;
    memcpy(buf + 594, vol, vl);

    return buf;
}

// ============================================================
// Data writing helpers
// ============================================================

static void write_pregap(FILE *f, int sectors, int ssize) {
    std::vector<uint8_t> zero(ssize, 0);
    if (ssize == 2336) {
        // Mode2 subheader pattern: byte 2 and 6 = 0x20
        zero[2] = 0x20; zero[6] = 0x20;
    }
    for (int i = 0; i < sectors; i++)
        fwrite(zero.data(), 1, ssize, f);
}

static void write_iso_sectors(FILE *cdi, FILE *iso, int num_sectors) {
    uint8_t subhdr[8] = { 0, 0, 0x20, 0, 0, 0, 0x20, 0 };
    uint8_t data[2048];
    uint8_t edc_ecc[280] = {}; // zeros for EDC+ECC area (no ECC generated)

    for (int i = 0; i < num_sectors; i++) {
        size_t n = fread(data, 1, 2048, iso);
        if (n < 2048) memset(data + n, 0, 2048 - n);
        fwrite(subhdr, 1, 8, cdi);
        fwrite(data, 1, 2048, cdi);
        fwrite(edc_ecc, 1, 280, cdi);
    }
}

// ============================================================
// Main
// ============================================================

static void print_usage(const char *prog) {
    printf("CDI Builder - Fast DiscJuggler CDI image builder\n\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -d <dir>      Input directory with game files + IP.BIN (runs mkisofs)\n");
    printf("  -I <iso>      Input ISO file (skips mkisofs)\n");
    printf("  -o <cdi>      Output CDI file (default: <volume>.cdi)\n");
    printf("  -V <name>     Volume name (default: dcgame)\n");
    printf("  -l <lba>      Session 2 LBA: 11702 for audio/data, 45000 for data/data\n");
    printf("  -t <type>     Image type: audio or data (default: audio)\n");
    printf("  -h, --help    Show this help\n");
}

int main(int argc, char **argv) {
    const char *input_dir  = nullptr;
    const char *input_iso  = nullptr;
    std::string output_cdi;
    const char *volume     = "dcgame";
    int         lba        = 11702;
    bool        data_mode  = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            input_dir = argv[++i];
        else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc)
            input_iso = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output_cdi = argv[++i];
        else if (strcmp(argv[i], "-V") == 0 && i + 1 < argc)
            volume = argv[++i];
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
            lba = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            data_mode = (strcmp(argv[++i], "data") == 0);
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]); return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]); return 1;
        }
    }

    if (!input_dir && !input_iso) { print_usage(argv[0]); return 1; }

    if (output_cdi.empty())
        output_cdi = std::string(volume) + ".cdi";

    // === Create ISO (if directory was given) ===
    std::string iso_path;
    std::string tmp_iso;
    bool tmp = false;

    if (input_iso) {
        iso_path = input_iso;
    } else {
        tmp_iso = std::string(volume) + ".tmp.iso";
        iso_path = tmp_iso;
        tmp = true;

        // Find boot sector: bootsector.bin > IP.BIN priority.
        // Both files are moved OUT of the input dir during mkisofs so they
        // don't appear in the ISO filesystem, then restored afterward.
        std::string boot_g_path;  // path for mkisofs -G
        std::string bs_path = std::string(input_dir) + "/bootsector.bin";
        std::string ip_path = std::string(input_dir) + "/IP.BIN";
        std::string bs_temp = "_cdibuild_boot.tmp";   // temp location for -G source
        std::string ip_temp = "_cdibuild_ip.tmp";
        bool have_boot = false;

        FILE *test = fopen(bs_path.c_str(), "rb");
        if (test) { fclose(test); have_boot = true;
            rename(bs_path.c_str(), bs_temp.c_str());   // move out of input dir
            rename(ip_path.c_str(), ip_temp.c_str());   // move IP.BIN too (may fail)
            boot_g_path = bs_temp;
        } else {
            test = fopen(ip_path.c_str(), "rb");
            if (test) { fclose(test); have_boot = true;
                rename(ip_path.c_str(), ip_temp.c_str());
                boot_g_path = ip_temp;
            }
        }

        if (!have_boot)
            fprintf(stderr, "WARNING: No bootsector.bin or IP.BIN found in %s\n", input_dir);

        char cmd[4096];
        // -l = allow long ISO9660 names (31 chars), no -r/-J to keep case.
        if (have_boot) {
            if (data_mode)
                snprintf(cmd, sizeof(cmd),
                    "mkisofs -V \"%s\" -l -G \"%s\" -o \"%s\" \"%s\"",
                    volume, boot_g_path.c_str(), iso_path.c_str(), input_dir);
            else
                snprintf(cmd, sizeof(cmd),
                    "mkisofs -C 0,%d -V \"%s\" -l -G \"%s\" -o \"%s\" \"%s\"",
                    lba, volume, boot_g_path.c_str(), iso_path.c_str(), input_dir);
        } else {
            if (data_mode)
                snprintf(cmd, sizeof(cmd),
                    "mkisofs -V \"%s\" -l -o \"%s\" \"%s\"",
                    volume, iso_path.c_str(), input_dir);
            else
                snprintf(cmd, sizeof(cmd),
                    "mkisofs -C 0,%d -V \"%s\" -l -o \"%s\" \"%s\"",
                    lba, volume, iso_path.c_str(), input_dir);
        }
        printf("Running: mkisofs ...\n");
        int ret = system(cmd);
        if (ret != 0)
            fprintf(stderr, "WARNING: mkisofs returned %d\n", ret);

        // Restore files to input directory
        rename(bs_temp.c_str(), bs_path.c_str());
        rename(ip_temp.c_str(), ip_path.c_str());
    }

    // === Read ISO ===
    FILE *iso = fopen(iso_path.c_str(), "rb");
    if (!iso) {
        fprintf(stderr, "Cannot open: %s\n", iso_path.c_str());
        error_exit("ISO file not found");
    }

    fseeko(iso, 0, SEEK_END);
    int64_t iso_size = ftello(iso);
    fseeko(iso, 0, SEEK_SET);
    int iso_sectors = (int)((iso_size + 2047) / 2048);

    printf("ISO: %lld bytes, %d sectors\n", (long long)iso_size, iso_sectors);
    printf("Mode: %s, LBA: %d\n", data_mode ? "data/data" : "audio/data", lba);

    const int DS = 2336;  // Mode2/2336 sector size
    const int AS = 2352;  // Audio sector size
    const int PG = 150;   // pregap sectors

    // === Write CDI ===
    FILE *cdi = fopen(output_cdi.c_str(), "wb");
    if (!cdi) error_exit("Cannot create output CDI");

    // --- Session 1 ---
    int32_t s1_data_len;   // post-pregap sectors
    int     s1_ssize;      // sector size
    int32_t s1_mode;

    if (data_mode) {
        s1_data_len = iso_sectors + 2;  // ISO + 2 postgap
        s1_ssize = DS;
        s1_mode = 2; // Mode2
        write_pregap(cdi, PG, DS);
        fseeko(iso, 0, SEEK_SET);
        write_iso_sectors(cdi, iso, iso_sectors);
        write_pregap(cdi, 2, DS);
    } else {
        s1_data_len = 302 + 2;  // 302 silence sectors + 2 postgap
        s1_ssize = AS;
        s1_mode = 0; // Audio
        write_pregap(cdi, PG, AS);
        write_pregap(cdi, 302, AS);
        write_pregap(cdi, 2, AS);
    }

    // --- Session 2 (data) ---
    int32_t s2_data_len;
    if (data_mode) {
        // Small bootstrap track: IP.BIN (16 sectors) + padding
        s2_data_len = 16 + 300 + 2;  // IP.BIN + padding + postgap
        write_pregap(cdi, PG, DS);
        fseeko(iso, 0, SEEK_SET);
        write_iso_sectors(cdi, iso, 16);      // IP.BIN (first 16 sectors of ISO)
        write_pregap(cdi, 300, DS);            // padding
        write_pregap(cdi, 2, DS);              // postgap
    } else {
        // Real data goes here
        s2_data_len = iso_sectors + 2;
        write_pregap(cdi, PG, DS);
        fseeko(iso, 0, SEEK_SET);
        write_iso_sectors(cdi, iso, iso_sectors);
        write_pregap(cdi, 2, DS);
    }

    fclose(iso);
    if (tmp) remove(iso_path.c_str());

    // === Write CDI headers (using golden blob template) ===
    int64_t header_pos = ftello(cdi);

    if (data_mode) {
        // TODO: data/data golden blob
        fprintf(stderr, "WARNING: data/data mode not yet supported with golden headers\n");
        fclose(cdi);
        return 1;
    }

    // Use the exact 665-byte header from a real DiscJuggler audio/data CDI.
    // Only dynamic fields (sector counts, LBA, volume name) are patched.
    uint8_t *hdr = build_header_audio(
        PG, s1_data_len,        // session 1: pregap + data sectors
        PG, s2_data_len,         // session 2: pregap + data sectors
        lba, volume);

    fwrite(hdr, 1, 665, cdi);
    free(hdr);

    // === Footer (8 bytes) ===
    int64_t end_pos = ftello(cdi);
    uint32_t hdr_offset = (uint32_t)(end_pos - header_pos) + 8;
    uint32_t version = CDI_V35;
    fwrite(&version, 4, 1, cdi);
    fwrite(&hdr_offset, 4, 1, cdi);

    fclose(cdi);

    printf("\nDone: %s  (%.1f MB)\n", output_cdi.c_str(),
           (double)end_pos / (1024 * 1024));
    return 0;
}
