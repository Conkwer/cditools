#pragma once

#include <cstdint>
#include <cstddef>

namespace embedded {

// katana.bin
extern const uint8_t katana[32768];
constexpr size_t katana_size = 32768;

// wince.bin
extern const uint8_t wince[32768];
constexpr size_t wince_size = 32768;

// kos.bin
extern const uint8_t kos[32768];
constexpr size_t kos_size = 32768;

// lodoss-5167.bin
extern const uint8_t lodoss_5167[32768];
constexpr size_t lodoss_5167_size = 32768;

// logo.mr
extern const uint8_t logo_mr[4109];
constexpr size_t logo_mr_size = 4109;

// wince.mr
extern const uint8_t wince_mr[5890];
constexpr size_t wince_mr_size = 5890;

} // namespace embedded