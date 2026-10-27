// SPDX-License-Identifier: MIT
// Piano 0.7c: the grand table (grand_live_table.bin, made by mvp/piano-grand/build_live_table.py) embedded in the binary with
// .incbin (GNU assembler: Linux). Other platforms and builds without the file return no table (Body Grand then fails to prepare).
#include "grand_live.hpp"
#if defined(PIANO_GRAND_TABLE_PATH) && defined(__linux__) && (defined(__GNUC__) || defined(__clang__))
__asm__(".section .rodata\n.balign 64\n.global piano_grand_table_begin\npiano_grand_table_begin:\n.incbin \"" PIANO_GRAND_TABLE_PATH "\"\n"
        ".global piano_grand_table_end\npiano_grand_table_end:\n.byte 0\n.previous\n");
extern "C" const std::uint8_t piano_grand_table_begin[], piano_grand_table_end[];
namespace piano_mvp_v4 {
const std::uint8_t* embeddedGrandTable(std::size_t& size) noexcept {
    size = static_cast<std::size_t>(piano_grand_table_end - piano_grand_table_begin);
    return piano_grand_table_begin;
}
}
#else
namespace piano_mvp_v4 {
const std::uint8_t* embeddedGrandTable(std::size_t& size) noexcept { size = 0; return nullptr; }
}
#endif
