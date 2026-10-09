#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace truetype {
enum class Result { Supported, Unsupported, Unavailable };
inline uint32_t u32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
// Bounded preflight, before FreeType or large allocations. No font-data copy:
// one 16-byte record on the stack, checked offsets, no unaligned integer reads.
// This enforces app policy; FreeType still validates the face and Unicode cmap.
template <typename Read>
Result inspect(size_t size, Read read) {
  if (size < 12) return Result::Unsupported;
  uint8_t record[16];
  if (!read(0, record, 12)) return Result::Unavailable;
  if (u32(record) != 0x00010000 && std::memcmp(record, "true", 4) != 0)
    return Result::Unsupported;  // Includes OTTO (CFF/CFF2), TTC, WOFF/WOFF2.
  const unsigned tables = (unsigned(record[4]) << 8) | record[5];
  const size_t directoryEnd = 12 + size_t(tables) * 16;
  if (!tables || directoryEnd > size) return Result::Unsupported;
  uint32_t previous = 0;
  unsigned required = 0;
  for (unsigned i = 0; i < tables; ++i) {
    if (!read(12 + size_t(i) * 16, record, sizeof(record))) return Result::Unavailable;
    const uint32_t tag = u32(record);
    const size_t offset = u32(record + 8), length = u32(record + 12);
    // SFNT records must be sorted by tag. This also rejects duplicate tables.
    if (tag <= previous || offset < directoryEnd || offset > size || length > size - offset) return Result::Unsupported;
    previous = tag;
    if (tag == 0x43464620 || tag == 0x43464632 || tag == 0x66766172 || tag == 0x67766172)
      return Result::Unsupported;  // CFF, CFF2, fvar, gvar: no static TrueType face.
    switch (tag) {
      case 0x676c7966:
        if (!length) return Result::Unsupported;
        required |= 1;
        break;  // glyf
      case 0x6c6f6361:
        if (length < 4) return Result::Unsupported;
        required |= 2;
        break;  // loca
      case 0x68656164:
        if (length < 54) return Result::Unsupported;
        required |= 4;
        break;  // head
      case 0x6d617870:
        if (length < 32) return Result::Unsupported;
        required |= 8;
        break;  // maxp
      case 0x636d6170:
        if (length < 4) return Result::Unsupported;
        required |= 16;
        break;  // cmap
      case 0x68686561:
        if (length < 36) return Result::Unsupported;
        required |= 32;
        break;  // hhea
      case 0x686d7478:
        if (length < 4) return Result::Unsupported;
        required |= 64;
        break;  // hmtx
    }
  }
  return required == 127 ? Result::Supported : Result::Unsupported;
}
}  // namespace truetype
