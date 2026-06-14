#define LIBISOFS_WITHOUT_LIBBURN

#include "libisofs_wrapper.hpp"
#include "third_party/libisofs/libisofs/libisofs.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

bool create_dreamcast_iso(const std::string& data_dir,
                          const std::string& output_iso,
                          unsigned int lba,
                          const std::string& volume_name,
                          const std::string& sort_file,
                          bool quiet) {
    int ret;

    // --- 1. Initialize libisofs ---
    ret = iso_init();
    if (ret < 0) {
        std::cerr << "libisofs: iso_init() failed: " << iso_error_to_msg(ret) << "\n";
        return false;
    }

    // --- 2. Read IP.BIN for system area ---
    std::string ip_path = data_dir + "/IP.BIN";
    std::ifstream ipf(ip_path, std::ios::binary);
    uint8_t ip_bin[32768] = {};
    bool have_ip = false;
    if (ipf) {
        ipf.read(reinterpret_cast<char*>(ip_bin), sizeof(ip_bin));
        have_ip = ipf.gcount() > 0;
    }

    // --- 3. Create image ---
    IsoImage* img = nullptr;
    ret = iso_image_new(volume_name.c_str(), &img);
    if (ret < 0) {
        std::cerr << "libisofs: iso_image_new() failed: " << iso_error_to_msg(ret) << "\n";
        iso_finish();
        return false;
    }

    // --- 4. Configure tree behavior ---
    iso_tree_set_follow_symlinks(img, 0);
    iso_tree_set_ignore_hidden(img, 0);
    iso_tree_set_ignore_special(img, 0);

    // --- 5. Add all files from data directory ---
    IsoDir* root = iso_image_get_root(img);
    if (!root) {
        std::cerr << "libisofs: cannot get root\n";
        iso_image_unref(img);
        iso_finish();
        return false;
    }

    // Add entire data directory recursively (matches mkdcdisc approach).
    // IP.BIN will be in the filesystem — we remove it below.
    try {
        ret = iso_tree_add_dir_rec(img, root, data_dir.c_str());
        if (ret < 0) {
            std::cerr << "libisofs: iso_tree_add_dir_rec failed: "
                      << iso_error_to_msg(ret) << "\n";
            iso_image_unref(img);
            iso_finish();
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "libisofs: error scanning data dir: " << e.what() << "\n";
        iso_image_unref(img);
        iso_finish();
        return false;
    }

    // --- 6. Create write options ---
    IsoWriteOpts* opts = nullptr;
    ret = iso_write_opts_new(&opts, 0);
    if (ret < 0) {
        std::cerr << "libisofs: iso_write_opts_new() failed: " << iso_error_to_msg(ret) << "\n";
        iso_image_unref(img);
        iso_finish();
        return false;
    }

    // Remove IP.BIN from filesystem — it's in the system area, not a regular file
    {
        IsoDirIter* iter = NULL;
        if (iso_dir_get_children(root, &iter) == 1) {
            IsoNode* node;
            while (iso_dir_iter_next(iter, &node) == 1 && node) {
                if (!strcmp(iso_node_get_name(node), "IP.BIN")) {
                    iso_node_remove(node);
                    break;
                }
            }
            iso_dir_iter_free(iter);
        }
    }

    iso_write_opts_set_joliet(opts, 1);
    iso_write_opts_set_rockridge(opts, 1);
    iso_write_opts_set_allow_dir_id_ext(opts, 1);

    // Session 2 offset (LBA)
    iso_write_opts_set_ms_block(opts, lba);

    // IP.BIN as system area (32KB boot sector)
    if (have_ip) {
        iso_write_opts_set_system_area(opts, reinterpret_cast<char*>(ip_bin), 0, 0);
    }

    // --- 7. Generate ISO into burn_source ---
    struct burn_source* burn_src = nullptr;
    ret = iso_image_create_burn_source(img, opts, &burn_src);
    if (ret < 0) {
        std::cerr << "libisofs: iso_image_create_burn_source() failed: "
                  << iso_error_to_msg(ret) << "\n";
        iso_write_opts_free(opts);
        iso_image_unref(img);
        iso_finish();
        return false;
    }

    // --- 8. Read ISO data and write to file ---
    std::ofstream out(output_iso, std::ios::binary);
    if (!out) {
        std::cerr << "libisofs: cannot create " << output_iso << "\n";
        burn_src->free_data(burn_src);
        free(burn_src);
        iso_write_opts_free(opts);
        iso_image_unref(img);
        iso_finish();
        return false;
    }

    unsigned char buf[2048];
    int total_blocks = 0;
    int block;
    while ((block = burn_src->read_xt(burn_src, buf, 2048)) > 0) {
        out.write(reinterpret_cast<const char*>(buf), block);
        total_blocks++;
    }
    out.close();

    if (!quiet) {
        std::cout << "  ISO created: " << total_blocks << " extents ("
                  << (total_blocks * 2048 / 1024 / 1024) << " MB)\n";
    }

    // --- 9. Cleanup ---
    burn_src->free_data(burn_src);
    free(burn_src);
    iso_write_opts_free(opts);
    // iso_image_unref not called - free_data handles it (per mkdcdisc pattern)

    iso_finish();
    return true;
}
