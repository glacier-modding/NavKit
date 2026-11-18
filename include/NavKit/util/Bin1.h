#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace Bin1 {
    constexpr size_t headerSize = 0x10;
    constexpr uint64_t nullPointer = std::numeric_limits<uint64_t>::max();
    constexpr uint32_t relocationSegmentId = 0x12eba5ed;
    constexpr uint32_t typeSegmentId = 0x3989bf9f;

    template <typename T> void writeLittleEndian(std::vector<uint8_t>& bytes, const size_t offset, const T value) {
        static_assert(std::is_arithmetic_v<T>);
        std::array<uint8_t, sizeof(T)> valueBytes{};
        std::memcpy(valueBytes.data(), &value, sizeof(T));
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(valueBytes);
        }
        if (offset > bytes.max_size() - valueBytes.size()) {
            throw std::length_error("BIN1 data is too large to serialize.");
        }
        if (bytes.size() < offset + valueBytes.size()) {
            bytes.resize(offset + valueBytes.size());
        }
        std::ranges::copy(valueBytes, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    template <typename T> void appendLittleEndian(std::vector<uint8_t>& bytes, const T value) {
        writeLittleEndian(bytes, bytes.size(), value);
    }

    template <typename T> T readLittleEndian(const std::vector<uint8_t>& bytes, const size_t offset) {
        static_assert(std::is_arithmetic_v<T>);
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
            throw std::runtime_error("Unexpected end of BIN1 data.");
        }
        std::array<uint8_t, sizeof(T)> valueBytes{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(T), valueBytes.begin());
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(valueBytes);
        }
        T value;
        std::memcpy(&value, valueBytes.data(), sizeof(T));
        return value;
    }

    inline uint32_t readDataSize(const std::vector<uint8_t>& bytes, const size_t rootSize) {
        if (bytes.size() < headerSize || std::memcmp(bytes.data(), "BIN1", 4) != 0) {
            throw std::runtime_error("File does not have a valid BIN1 header.");
        }
        const uint32_t dataSize = (static_cast<uint32_t>(bytes[8]) << 24) | (static_cast<uint32_t>(bytes[9]) << 16) |
            (static_cast<uint32_t>(bytes[10]) << 8) | static_cast<uint32_t>(bytes[11]);
        if (dataSize < rootSize || dataSize > bytes.size() - headerSize) {
            throw std::runtime_error("BIN1 data segment has an invalid size.");
        }
        return dataSize;
    }

    struct Array {
        size_t offset;
        size_t count;
    };

    inline Array readArray(const std::vector<uint8_t>& bytes, const size_t pointerOffset, const uint32_t dataSize,
        const size_t rootSize, const size_t elementSize) {
        const uint64_t begin = readLittleEndian<uint64_t>(bytes, pointerOffset);
        const uint64_t end = readLittleEndian<uint64_t>(bytes, pointerOffset + sizeof(uint64_t));
        const uint64_t allocationEnd = readLittleEndian<uint64_t>(bytes, pointerOffset + 2 * sizeof(uint64_t));
        if (begin == nullPointer && end == begin && allocationEnd == begin) {
            return {0, 0};
        }
        if (end < begin || allocationEnd < end || allocationEnd > dataSize) {
            throw std::runtime_error("BIN1 array pointers are invalid.");
        }
        const uint64_t size = end - begin;
        if (size % elementSize != 0) {
            throw std::runtime_error("BIN1 array size does not match its element type.");
        }
        if (size == 0) {
            return {0, 0};
        }
        if (begin < rootSize) {
            throw std::runtime_error("BIN1 array points into its root object.");
        }
        const size_t offset = headerSize + static_cast<size_t>(begin);
        if (offset > bytes.size() || size > bytes.size() - offset) {
            throw std::runtime_error("BIN1 array extends beyond the end of the file.");
        }
        return {offset, static_cast<size_t>(size / elementSize)};
    }

    class Writer {
    public:
        explicit Writer(const size_t rootSize) : data(rootSize, 0) {}

        template <typename T> void put(const size_t offset, const T value) {
            writeLittleEndian(data, offset, value);
        }

        template <typename T> void append(const T value) {
            appendLittleEndian(data, value);
        }

        void putPointers(const size_t pointerOffset, const uint64_t begin, const uint64_t end) {
            put(pointerOffset, begin);
            put(pointerOffset + 8, end);
            put(pointerOffset + 16, end);
            for (size_t field = 0; field < 3; ++field) {
                relocations.push_back(checkedUint32(pointerOffset + field * 8));
            }
        }

        void putNullArray(const size_t pointerOffset) {
            putPointers(pointerOffset, nullPointer, nullPointer);
        }

        // Writes a count-prefixed array; an empty one still takes a byte so the next array gets its own address.
        template <typename AppendElements>
        size_t appendArray(const size_t pointerOffset, const size_t count, const size_t alignment,
            AppendElements appendElements) {
            const size_t begin = (data.size() + 4 + alignment - 1) / alignment * alignment;
            data.resize(begin - 4, 0);
            append(checkedUint32(count));
            appendElements();
            putPointers(pointerOffset, begin, data.size());
            if (count == 0) {
                data.push_back(0);
            }
            return begin;
        }

        std::vector<uint8_t> finish(const uint8_t alignment, const uint8_t version) {
            const uint32_t dataSize = checkedUint32(data.size());
            std::vector<uint8_t> file = {'B', 'I', 'N', '1', 0, alignment, version, 0,
                static_cast<uint8_t>(dataSize >> 24), static_cast<uint8_t>(dataSize >> 16),
                static_cast<uint8_t>(dataSize >> 8), static_cast<uint8_t>(dataSize), 0, 0, 0, 0};
            file.insert(file.end(), data.begin(), data.end());

            std::ranges::sort(relocations);
            appendLittleEndian(file, relocationSegmentId);
            appendLittleEndian(file, checkedUint32(4 + relocations.size() * sizeof(uint32_t)));
            appendLittleEndian(file, checkedUint32(relocations.size()));
            for (const uint32_t relocation : relocations) {
                appendLittleEndian(file, relocation);
            }
            return file;
        }

        std::vector<uint8_t> data;

    private:
        static uint32_t checkedUint32(const size_t value) {
            if (value > std::numeric_limits<uint32_t>::max()) {
                throw std::length_error("BIN1 data is too large to serialize.");
            }
            return static_cast<uint32_t>(value);
        }

        std::vector<uint32_t> relocations;
    };

    inline void appendEmptyTypeSegment(std::vector<uint8_t>& file) {
        appendLittleEndian(file, typeSegmentId);
        appendLittleEndian(file, uint32_t{8});
        appendLittleEndian(file, uint64_t{0});
    }
} // namespace Bin1
