//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_SERIAL_HPP
#define BUSINESS_GAME_SIM_SERIAL_HPP
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sim/Fixed.hpp"

namespace sim {

/**
 * Bytes out, little endian whatever the machine is, so that what one machine
 * writes another reads back the same. Used for commands (the replay log now,
 * the network later) and for the simulation's state, which is also what its
 * checksum is taken over.
 *
 * voxel/VoxelFile.cpp has its own stream based helpers for the same job. They
 * are kept apart because that file sits on the raylib side of the line.
 */
class ByteWriter {
public:
    void write_u8(uint8_t v) { bytes.push_back(v); }
    void write_u16(uint16_t v) { write_le(v, 2); }
    void write_u32(uint32_t v) { write_le(v, 4); }
    void write_u64(uint64_t v) { write_le(v, 8); }
    void write_i8(int8_t v) { write_u8(static_cast<uint8_t>(v)); }
    void write_i64(int64_t v) { write_u64(static_cast<uint64_t>(v)); }
    void write_fixed(Fixed v) { write_i64(v.raw); }

    /** A u32 length, then the characters. */
    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }

    void write_bytes(std::span<const uint8_t> b) { bytes.insert(bytes.end(), b.begin(), b.end()); }

    const std::vector<uint8_t>& data() const { return bytes; }

private:
    std::vector<uint8_t> bytes;

    void write_le(uint64_t v, int count) {
        for (int i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
};

/**
 * Bytes in. Every read reports whether there were enough bytes left, and once
 * one fails the reader stays failed, so a caller can read a whole record and
 * check ok() once at the end.
 */
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> bytes) : bytes(bytes) {}

    bool read_u8(uint8_t* out) { return read_le(out, 1); }
    bool read_u16(uint16_t* out) { return read_le(out, 2); }
    bool read_u32(uint32_t* out) { return read_le(out, 4); }
    bool read_u64(uint64_t* out) { return read_le(out, 8); }

    bool read_i8(int8_t* out) {
        uint8_t v = 0;
        if (!read_u8(&v)) return false;
        *out = static_cast<int8_t>(v);
        return true;
    }

    bool read_i64(int64_t* out) {
        uint64_t v = 0;
        if (!read_u64(&v)) return false;
        *out = static_cast<int64_t>(v);
        return true;
    }

    bool read_fixed(Fixed* out) { return read_i64(&out->raw); }

    /** Refuses anything longer than `max_length`, so a corrupt length cannot ask for gigabytes. */
    bool read_string(std::string* out, uint32_t max_length = 1024) {
        uint32_t length = 0;
        if (!read_u32(&length)) return false;
        if (length > max_length || remaining() < length) return fail();
        out->assign(reinterpret_cast<const char*>(bytes.data() + pos), length);
        pos += length;
        return true;
    }

    /** The next `count` bytes as a span of their own, for reading a record with its own reader. */
    bool read_bytes(std::span<const uint8_t>* out, size_t count) {
        if (remaining() < count) return fail();
        *out = bytes.subspan(pos, count);
        pos += count;
        return true;
    }

    size_t remaining() const { return failed ? 0 : bytes.size() - pos; }
    bool ok() const { return !failed; }
    bool at_end() const { return !failed && pos == bytes.size(); }

private:
    std::span<const uint8_t> bytes;
    size_t pos = 0;
    bool failed = false;

    bool fail() { failed = true; return false; }

    template <typename U>
    bool read_le(U* out, int count) {
        if (failed || bytes.size() - pos < static_cast<size_t>(count)) return fail();
        uint64_t v = 0;
        for (int i = 0; i < count; ++i) v |= static_cast<uint64_t>(bytes[pos + i]) << (8 * i);
        pos += count;
        *out = static_cast<U>(v);
        return true;
    }
};

/** FNV-1a, 64 bit. Small, portable, and plenty for telling two states apart. */
inline uint64_t fnv1a(std::span<const uint8_t> bytes) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const uint8_t b : bytes) {
        hash ^= b;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

} // namespace sim

#endif //BUSINESS_GAME_SIM_SERIAL_HPP
