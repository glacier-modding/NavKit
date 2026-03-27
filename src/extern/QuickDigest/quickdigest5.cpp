/*
 * Copyright (c) 2024 - Nathanne Isip
 * This file is part of QuickDigest5.
 * 
 * N8 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 * 
 * N8 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with N8. If not, see <https://www.gnu.org/licenses/>.
 */

#include "../../include/QuickDigest/quickdigest5.hpp"

uint8_t QuickDigest5::padding[64] = {0x80};

uint32_t QuickDigest5::shift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22,
    7, 12, 17, 22, 7, 12, 17, 22,

    5,  9, 14, 20, 5,  9, 14, 20,
    5,  9, 14, 20, 5,  9, 14, 20,

    4, 11, 16, 23, 4, 11, 16, 23,
    4, 11, 16, 23, 4, 11, 16, 23,

    6, 10, 15, 21, 6, 10, 15, 21,
    6, 10, 15, 21, 6, 10, 15, 21
};

uint32_t QuickDigest5::sineDerivation[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static inline uint32_t rotateLeft(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

void QuickDigest5::step(const std::vector<uint32_t>& inputVec) {
    uint32_t buf_0 = this->buffer[0];
    uint32_t buf_1 = this->buffer[1];
    uint32_t buf_2 = this->buffer[2];
    uint32_t buf_3 = this->buffer[3];

    for(uint32_t i = 0; i < 64; ++i) {
        uint32_t bit, idx;
        switch(i / 16) {
            case 0:
                bit = (buf_1 & buf_2) | (~buf_1 & buf_3);
                idx = i;
                break;

            case 1:
                bit = (buf_1 & buf_3) | (buf_2 & ~buf_3);
                idx = ((i * 5) + 1) % 16;
                break;

            case 2:
                bit = buf_1 ^ buf_2 ^ buf_3;
                idx = ((i * 3) + 5) % 16;
                break;

            default:
                bit = buf_2 ^ (buf_1 | ~buf_3);
                idx = (i * 7) % 16;
                break;
        }

        uint32_t temp = buf_3;
        buf_3 = buf_2;
        buf_2 = buf_1;
        buf_1 = buf_1 + rotateLeft(
            buf_0 + bit + QuickDigest5::sineDerivation[i] + inputVec[idx],
            this->shift[i]
        );
        buf_0 = temp;
    }

    this->buffer[0] += buf_0;
    this->buffer[1] += buf_1;
    this->buffer[2] += buf_2;
    this->buffer[3] += buf_3;
}

void QuickDigest5::update(const uint8_t* inputBuffer, size_t inputLength) {
    uint32_t offset = size % 64;
    this->size += inputLength;

    for(size_t i = 0; i < inputLength; ++i) {
        this->input[offset++] = inputBuffer[i];

        if(offset % 64 == 0) {
            std::vector<uint32_t> block(16);
            for(int idx = 0; idx < 16; ++idx)
                block[(size_t) idx] =
                    (uint32_t) (this->input[(size_t) (idx * 4) + 3]) << 24 |
                    (uint32_t) (this->input[(size_t) (idx * 4) + 2]) << 16 |
                    (uint32_t) (this->input[(size_t) (idx * 4) + 1]) << 8 |
                    (uint32_t) (this->input[(size_t) (idx * 4)]);

            QuickDigest5::step(block);
            offset = 0;
        }
    }
}

void QuickDigest5::finalize() {
    uint32_t offset = size % 64,
        padding_length = offset < 56 ?
            56 - offset :
            120 - offset;

    QuickDigest5::update(this->padding, padding_length);
    this->size -= padding_length;

    std::vector<uint32_t> block(16, 0);
    for(int idx = 0; idx < 14; ++idx)
        block[(size_t) idx] =
            (uint32_t) (this->input[(size_t) (idx * 4) + 3]) << 24 |
            (uint32_t) (this->input[(size_t) (idx * 4) + 2]) << 16 |
            (uint32_t) (this->input[(size_t) (idx * 4) + 1]) << 8 |
            (uint32_t) (this->input[(size_t) (idx * 4)]);

    block[14] = (uint32_t) (this->size * 8);
    block[15] = (uint32_t) ((this->size * 8) >> 32);
    QuickDigest5::step(block);

    for(int i = 0; i < 4; ++i) {
        this->digest[(size_t) (i * 4) + 0] =
            (uint8_t) ((this->buffer[(size_t) i] & 0x000000ff));
        this->digest[(size_t) (i * 4) + 1] =
            (uint8_t) ((this->buffer[(size_t) i] & 0x0000ff00) >> 8);
        this->digest[(size_t) (i * 4) + 2] =
            (uint8_t) ((this->buffer[(size_t) i] & 0x00ff0000) >> 16);
        this->digest[(size_t) (i * 4) + 3] =
            (uint8_t) ((this->buffer[(size_t) i] & 0xff000000) >> 24);
    }
}

QuickDigest5::QuickDigest5() :
    size(0),
    buffer(4),
    input(64),
    digest(16)
{
    buffer[0] = 0x67452301;
    buffer[1] = 0xefcdab89;
    buffer[2] = 0x98badcfe;
    buffer[3] = 0x10325476;
}

std::vector<uint8_t> QuickDigest5::digestString(const std::string& input) {
    QuickDigest5 QuickDigest5;
    QuickDigest5.update(
        reinterpret_cast<const uint8_t*>(input.c_str()),
        input.length()
    );
    QuickDigest5.finalize();

    return QuickDigest5.digest;
}

std::vector<uint8_t> QuickDigest5::digestFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        throw std::runtime_error("Cannot open file: " + filepath);

    QuickDigest5 QuickDigest5;
    std::vector<uint8_t> buffer(4096);

    while(file) {
        file.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize) buffer.size());

        std::streamsize bytesRead = file.gcount();
        if(bytesRead > 0)
            QuickDigest5.update(buffer.data(), (size_t) bytesRead);
    }

    QuickDigest5.finalize();
    return QuickDigest5.digest;
}

std::string QuickDigest5::toHash(const std::string& input) {
    std::vector<uint8_t> digest = QuickDigest5::digestString(input);
    std::stringstream ss;

    for(int i = 0; i < 16; ++i)
        ss << std::hex
            << std::setw(2)
            << std::setfill('0')
            << (int) digest[(size_t) i];

    return ss.str();
}

std::string QuickDigest5::fileToHash(const std::string& filepath) {
    std::vector<uint8_t> digest = QuickDigest5::digestFile(filepath);
    std::stringstream ss;

    for(int i = 0; i < 16; ++i)
        ss << std::hex
            << std::setw(2)
            << std::setfill('0')
            << (int) digest[(size_t) i];

    return ss.str();
}
