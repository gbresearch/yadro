//-----------------------------------------------------------------------------
//  Copyright (C) 2024, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//
//  Permission is hereby granted, free of charge, to any person or organization
//  obtaining a copy of the software and accompanying documentation covered by
//  this license (the "Software") to use, reproduce, display, distribute,
//  execute, and transmit the Software, and to prepare derivative works of the
//  Software, and to permit third-parties to whom the Software is furnished to
//  do so, all subject to the following:
//
//  The copyright notices in the Software and this entire statement, including
//  the above license grant, this restriction and the following disclaimer,
//  must be included in all copies of the Software, in whole or in part, and
//  all derivative works of the Software, unless such copies or derivative
//  works are solely in the form of machine-executable object code generated by
//  a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------


#include "string_util.h"
#include "gberror.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace gb::yadro::util
{
    md5::md5()
    {
        bitCount[0] = bitCount[1] = 0;
        state[0] = 0x67452301;
        state[1] = 0xefcdab89;
        state[2] = 0x98badcfe;
        state[3] = 0x10325476;
    }


    md5& md5::update(const uint8_t* data, size_t length)
    {
        gbassert(not finilized); // can't update finilized md5
        // Compute number of bytes mod 64
        size_t index = (bitCount[0] >> 3) & 0x3F;

        // Update number of bits
        if ((bitCount[0] += (length << 3)) < (length << 3)) {
            bitCount[1]++;
        }
        bitCount[1] += (length >> 29);

        size_t partLen = 64 - index;

        // Transform as many times as possible
        size_t i = 0;

        if (length >= partLen) {
            memcpy(&buffer[index], data, partLen);
            transform(buffer);

            for (i = partLen; i + 63 < length; i += 64) {
                transform(&data[i]);
            }

            index = 0;
        }

        // Buffer remaining input
        memcpy(&buffer[index], &data[i], length - i);
        return *this;
    }

    md5& md5::finilize()
    {
        if (not finilized)
        {
            uint8_t bits[8];
            constexpr uint8_t padding[64] = { 0x80 };

            // Save number of bits
            encode(bits, bitCount, 8);

            // Pad out to 56 mod 64
            size_t index = (bitCount[0] >> 3) & 0x3F;
            size_t padLen = (index < 56) ? (56 - index) : (120 - index);
            update(padding, padLen);

            // Append length (before padding)
            update(bits, 8);
        }
        finilized = true;
        return *this;
    }

    std::array<uint8_t, 16> md5::digest() const
    {
        gbassert(finilized); // must be finilized before calculating digest

        // Store state in digest
        std::array<uint8_t, 16> result;
        encode(result.data(), state, 16);
        return result;
    }

    std::string md5::to_string() const
    {
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (uint8_t byte : digest()) {
            result << std::setw(2) << static_cast<unsigned>(byte);
        }
        return result.str();
    }

    void md5::transform(const uint8_t block[64]) 
    {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t x[16];

        decode(x, block, 64);

        /* Round 1 */
        FF(a, b, c, d, x[0], S11, 0xd76aa478); /* 1 */
        FF(d, a, b, c, x[1], S12, 0xe8c7b756); /* 2 */
        FF(c, d, a, b, x[2], S13, 0x242070db); /* 3 */
        FF(b, c, d, a, x[3], S14, 0xc1bdceee); /* 4 */
        FF(a, b, c, d, x[4], S11, 0xf57c0faf); /* 5 */
        FF(d, a, b, c, x[5], S12, 0x4787c62a); /* 6 */
        FF(c, d, a, b, x[6], S13, 0xa8304613); /* 7 */
        FF(b, c, d, a, x[7], S14, 0xfd469501); /* 8 */
        FF(a, b, c, d, x[8], S11, 0x698098d8); /* 9 */
        FF(d, a, b, c, x[9], S12, 0x8b44f7af); /* 10 */
        FF(c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
        FF(b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
        FF(a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
        FF(d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
        FF(c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
        FF(b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

        /* Round 2 */
        GG(a, b, c, d, x[1], S21, 0xf61e2562); /* 17 */
        GG(d, a, b, c, x[6], S22, 0xc040b340); /* 18 */
        GG(c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
        GG(b, c, d, a, x[0], S24, 0xe9b6c7aa); /* 20 */
        GG(a, b, c, d, x[5], S21, 0xd62f105d); /* 21 */
        GG(d, a, b, c, x[10], S22, 0x02441453); /* 22 */
        GG(c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
        GG(b, c, d, a, x[4], S24, 0xe7d3fbc8); /* 24 */
        GG(a, b, c, d, x[9], S21, 0x21e1cde6); /* 25 */
        GG(d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
        GG(c, d, a, b, x[3], S23, 0xf4d50d87); /* 27 */
        GG(b, c, d, a, x[8], S24, 0x455a14ed); /* 28 */
        GG(a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
        GG(d, a, b, c, x[2], S22, 0xfcefa3f8); /* 30 */
        GG(c, d, a, b, x[7], S23, 0x676f02d9); /* 31 */
        GG(b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

        /* Round 3 */
        HH(a, b, c, d, x[5], S31, 0xfffa3942); /* 33 */
        HH(d, a, b, c, x[8], S32, 0x8771f681); /* 34 */
        HH(c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
        HH(b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
        HH(a, b, c, d, x[1], S31, 0xa4beea44); /* 37 */
        HH(d, a, b, c, x[4], S32, 0x4bdecfa9); /* 38 */
        HH(c, d, a, b, x[7], S33, 0xf6bb4b60); /* 39 */
        HH(b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
        HH(a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
        HH(d, a, b, c, x[0], S32, 0xeaa127fa); /* 42 */
        HH(c, d, a, b, x[3], S33, 0xd4ef3085); /* 43 */
        HH(b, c, d, a, x[6], S34, 0x04881d05); /* 44 */
        HH(a, b, c, d, x[9], S31, 0xd9d4d039); /* 45 */
        HH(d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
        HH(c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
        HH(b, c, d, a, x[2], S34, 0xc4ac5665); /* 48 */

        /* Round 4 */
        II(a, b, c, d, x[0], S41, 0xf4292244); /* 49 */
        II(d, a, b, c, x[7], S42, 0x432aff97); /* 50 */
        II(c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
        II(b, c, d, a, x[5], S44, 0xfc93a039); /* 52 */
        II(a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
        II(d, a, b, c, x[3], S42, 0x8f0ccc92); /* 54 */
        II(c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
        II(b, c, d, a, x[1], S44, 0x85845dd1); /* 56 */
        II(a, b, c, d, x[8], S41, 0x6fa87e4f); /* 57 */
        II(d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
        II(c, d, a, b, x[6], S43, 0xa3014314); /* 59 */
        II(b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
        II(a, b, c, d, x[4], S41, 0xf7537e82); /* 61 */
        II(d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
        II(c, d, a, b, x[2], S43, 0x2ad7d2bb); /* 63 */
        II(b, c, d, a, x[9], S44, 0xeb86d391); /* 64 */

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;

        // Zeroize sensitive information.
        std::memset(x, 0, sizeof x);
    }

    void md5::encode(uint8_t* output, const uint32_t* input, size_t length) 
    {
        for (size_t i = 0, j = 0; j < length; i++, j += 4) {
            output[j] = static_cast<uint8_t>(input[i] & 0xff);
            output[j + 1] = static_cast<uint8_t>((input[i] >> 8) & 0xff);
            output[j + 2] = static_cast<uint8_t>((input[i] >> 16) & 0xff);
            output[j + 3] = static_cast<uint8_t>((input[i] >> 24) & 0xff);
        }
    }

    void md5::decode(uint32_t* output, const uint8_t* input, size_t length) 
    {
        for (size_t i = 0, j = 0; j < length; i++, j += 4) {
            output[i] = static_cast<uint32_t>(input[j]) |
                (static_cast<uint32_t>(input[j + 1]) << 8) |
                (static_cast<uint32_t>(input[j + 2]) << 16) |
                (static_cast<uint32_t>(input[j + 3]) << 24);
        }
    }

}