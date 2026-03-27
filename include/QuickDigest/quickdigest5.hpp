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

/**
 * @file QuickDigest5.hpp
 * @author [Nathanne Isip](https://github.com/nthnn)
 * @brief A header file for the QuickDigest5 class, a utility for MD5 hashing.
 * 
 * This file defines the `QuickDigest5` class, which provides functionalities 
 * for computing MD5 hashes of strings and files. The class also includes 
 * static methods for converting the computed hash to a string representation.
 * 
 * @note This class is designed as a final class and cannot be inherited.
 */
#ifndef QUICKDIGEST5_HPP
#define QUICKDIGEST5_HPP

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/**
 * @class QuickDigest5
 * @brief A utility class for MD5 hashing of strings and files.
 * 
 * The `QuickDigest5` class provides methods to compute MD5 hashes 
 * for input strings and files, as well as converting the hash 
 * to a hexadecimal string representation. This class encapsulates
 * the internal MD5 computation logic and exposes static methods 
 * for ease of use.
 * 
 * @note This class is marked `final` and cannot be subclassed.
 */
class QuickDigest5 final {
private:
    /**
     * @brief Size of the input in bytes.
     */
    uint64_t size;

    /**
     * @brief Buffer for the MD5 computation.
     */
    std::vector<uint32_t> buffer;

    /**
     * @brief Input data to be processed for MD5 hashing.
     */
    std::vector<uint8_t> input;

    /**
     * @brief Stores the resulting MD5 hash.
     */
    std::vector<uint8_t> digest;

    /**
     * @brief Padding used during MD5 computation.
     * 
     * A static array defining the padding values required 
     * to align the input data to a 512-bit boundary.
     */
    static uint8_t padding[64];

    /**
     * @brief Shift values used during MD5 computation.
     * 
     * A static array defining the shift amounts for each 
     * step of the MD5 algorithm.
     */
    static uint32_t shift[64];

    /**
     * @brief Precomputed sine-derived constants.
     * 
     * A static array of constants derived from the fractional part 
     * of sine values, used in the MD5 algorithm.
     */
    static uint32_t sineDerivation[64];

    /**
     * @brief Finalizes the MD5 computation.
     * 
     * This method performs any final transformations required 
     * to complete the MD5 hash computation.
     */
    void finalize();

    /**
     * @brief Processes a single 512-bit block of input data.
     * 
     * @param inputVec A vector containing the input data to process.
     */
    void step(const std::vector<uint32_t>& inputVec);

    /**
     * @brief Updates the internal state with input data.
     * 
     * @param inputBuffer A pointer to the input data buffer.
     * @param inputLength The length of the input data in bytes.
     */
    void update(
        const uint8_t* inputBuffer,
        size_t inputLength
    );

protected:
    /**
     * @brief Constructs a `QuickDigest5` object.
     * 
     * The constructor is protected to prevent direct instantiation. 
     * Users should rely on the static methods provided by the class.
     */
    QuickDigest5();

public:
    /**
     * @brief Computes the MD5 hash of a string.
     * 
     * @param input The input string to hash.
     * @return A vector of bytes representing the MD5 hash.
     */
    static std::vector<uint8_t> digestString(const std::string& input);

    /**
     * @brief Computes the MD5 hash of a file.
     * 
     * @param filepath The path to the file to hash.
     * @return A vector of bytes representing the MD5 hash.
     */
    static std::vector<uint8_t> digestFile(const std::string& filepath);

    /**
     * @brief Computes the MD5 hash of a string and returns it as a hex string.
     * 
     * @param input The input string to hash.
     * @return A string representing the MD5 hash in hexadecimal format.
     */
    static std::string toHash(const std::string& input);

    /**
     * @brief Computes the MD5 hash of a file and returns it as a hex string.
     * 
     * @param filepath The path to the file to hash.
     * @return A string representing the MD5 hash in hexadecimal format.
     */
    static std::string fileToHash(const std::string& filepath);
};

#endif
