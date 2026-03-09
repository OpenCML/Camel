/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/string.h"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

namespace {
constexpr std::array<uint32_t, 64> K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

std::string sha256Hex(std::string_view data) {
    uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8u;
    std::string msg(data);
    msg.push_back(static_cast<char>(0x80));
    while ((msg.size() % 64) != 56) {
        msg.push_back('\0');
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xffu));
    }

    uint32_t h0 = 0x6a09e667u;
    uint32_t h1 = 0xbb67ae85u;
    uint32_t h2 = 0x3c6ef372u;
    uint32_t h3 = 0xa54ff53au;
    uint32_t h4 = 0x510e527fu;
    uint32_t h5 = 0x9b05688cu;
    uint32_t h6 = 0x1f83d9abu;
    uint32_t h7 = 0x5be0cd19u;

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i) {
            size_t j = chunk + static_cast<size_t>(i) * 4;
            w[i]     = (static_cast<uint32_t>(static_cast<uint8_t>(msg[j])) << 24) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(msg[j + 1])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(msg[j + 2])) << 8) |
                       static_cast<uint32_t>(static_cast<uint8_t>(msg[j + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i]        = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h0, b = h1, c = h2, d = h3;
        uint32_t e = h4, f = h5, g = h6, h = h7;
        for (int i = 0; i < 64; ++i) {
            uint32_t S1    = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch    = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0    = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
        h5 += f;
        h6 += g;
        h7 += h;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint32_t v : {h0, h1, h2, h3, h4, h5, h6, h7}) {
        oss << std::setw(8) << v;
    }
    return oss.str();
}
} // namespace

std::unordered_map<std::string, operator_t> getCryptoOpsMap() {
    return {
        {"sha256", __crypto_sha256__},
        {"sha256_rounds", __crypto_sha256_rounds__},
    };
}

slot_t __crypto_sha256__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *input = norm.get<String *>(0);
    return toSlot(String::from(sha256Hex(input->view()), mm::autoSpace()));
}

slot_t __crypto_sha256_rounds__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *input  = norm.get<String *>(0);
    int64_t rounds = norm.get<int64_t>(1);
    if (rounds < 0) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<sha256_rounds> requires rounds >= 0");
    }

    std::string value(input->view());
    for (int64_t i = 0; i < rounds; ++i) {
        value = sha256Hex(value);
    }
    return toSlot(String::from(value, mm::autoSpace()));
}
