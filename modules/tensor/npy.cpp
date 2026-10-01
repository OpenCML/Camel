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
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * `.npy` reader and writer (see npy.h). The header is a Python dict literal;
 * only the three keys the format defines ('descr', 'fortran_order', 'shape')
 * are parsed.
 */

#include "npy.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::tensor {

using type::TypeCode;

namespace {

constexpr char kMagic[] = "\x93NUMPY";

struct NpyHeader {
    char byteOrder  = '<';
    char kind       = 'f';
    size_t itemSize = 4;
    bool fortran    = false;
    Shape shape;
};

[[noreturn]] void fail(const std::filesystem::path &path, const std::string &what) {
    throw std::invalid_argument("npy '" + path.string() + "': " + what);
}

/// Value text following `'key':` in the header dict.
std::string
field(const std::string &header, const std::string &key, const std::filesystem::path &path) {
    const size_t at = header.find("'" + key + "'");
    if (at == std::string::npos) {
        fail(path, "header is missing '" + key + "'");
    }
    size_t pos = header.find(':', at);
    if (pos == std::string::npos) {
        fail(path, "malformed header");
    }
    ++pos;
    while (pos < header.size() && header[pos] == ' ') {
        ++pos;
    }
    return header.substr(pos);
}

NpyHeader parseHeader(const std::string &header, const std::filesystem::path &path) {
    NpyHeader result;
    const std::string descr = field(header, "descr", path);
    if (descr.size() < 5 || descr[0] != '\'') {
        fail(path, "unsupported descr");
    }
    result.byteOrder            = descr[1];
    result.kind                 = descr[2];
    result.itemSize             = static_cast<size_t>(std::stoul(descr.substr(3)));
    result.fortran              = field(header, "fortran_order", path).starts_with("True");
    const std::string shapeText = field(header, "shape", path);
    const size_t open = shapeText.find('('), close = shapeText.find(')');
    if (open == std::string::npos || close == std::string::npos) {
        fail(path, "malformed shape");
    }
    std::string inner = shapeText.substr(open + 1, close - open - 1);
    size_t pos        = 0;
    while (pos < inner.size()) {
        while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == ',')) {
            ++pos;
        }
        if (pos >= inner.size()) {
            break;
        }
        size_t used = 0;
        result.shape.push_back(std::stoll(inner.substr(pos), &used));
        pos += used;
    }
    return result;
}

template <typename T> T readElement(const std::byte *src, bool swap) {
    T value;
    std::memcpy(&value, src, sizeof(T));
    if (swap) {
        auto *bytes = reinterpret_cast<std::byte *>(&value);
        std::reverse(bytes, bytes + sizeof(T));
    }
    return value;
}

} // namespace

TensorObject *loadNpy(const std::filesystem::path &path, mm::IAllocator &allocator) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(path, "cannot open file");
    }
    char magic[6];
    in.read(magic, 6);
    if (!in || std::memcmp(magic, kMagic, 6) != 0) {
        fail(path, "not a .npy file");
    }
    unsigned char version[2];
    in.read(reinterpret_cast<char *>(version), 2);
    uint32_t headerLength = 0;
    if (version[0] == 1) {
        unsigned char len[2];
        in.read(reinterpret_cast<char *>(len), 2);
        headerLength = static_cast<uint32_t>(len[0]) | (static_cast<uint32_t>(len[1]) << 8);
    } else if (version[0] == 2 || version[0] == 3) {
        unsigned char len[4];
        in.read(reinterpret_cast<char *>(len), 4);
        headerLength = static_cast<uint32_t>(len[0]) | (static_cast<uint32_t>(len[1]) << 8) |
                       (static_cast<uint32_t>(len[2]) << 16) |
                       (static_cast<uint32_t>(len[3]) << 24);
    } else {
        fail(path, "unsupported format version " + std::to_string(version[0]));
    }
    std::string header(headerLength, '\0');
    in.read(header.data(), headerLength);
    if (!in) {
        fail(path, "truncated header");
    }
    const NpyHeader h = parseHeader(header, path);
    if (h.fortran) {
        fail(path, "Fortran-ordered arrays are not supported");
    }
    const bool hostLittle = std::endian::native == std::endian::little;
    const bool fileLittle =
        h.byteOrder == '<' || h.byteOrder == '|' || (h.byteOrder == '=' && hostLittle);
    const bool swap = fileLittle != hostLittle && h.itemSize > 1;

    TypeCode dtype;
    if (h.kind == 'f' && (h.itemSize == 4 || h.itemSize == 8)) {
        dtype = TypeCode::Float32;
    } else if (h.kind == 'i' && (h.itemSize == 4 || h.itemSize == 8)) {
        dtype = TypeCode::Int64;
    } else if (h.kind == 'b' && h.itemSize == 1) {
        dtype = TypeCode::Bool;
    } else {
        fail(
            path,
            std::string("unsupported element type '") + h.kind + std::to_string(h.itemSize) + "'");
    }

    TensorObject *tensor = TensorObject::create(dtype, h.shape, allocator);
    const uint64_t count = tensor->numel();
    std::vector<std::byte> raw(count * h.itemSize);
    in.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (static_cast<size_t>(in.gcount()) != raw.size()) {
        fail(path, "truncated data");
    }
    for (uint64_t i = 0; i < count; ++i) {
        const std::byte *src = raw.data() + i * h.itemSize;
        switch (dtype) {
        case TypeCode::Float32:
            tensor->dataAs<float>()[i] = h.itemSize == 4
                                             ? readElement<float>(src, swap)
                                             : static_cast<float>(readElement<double>(src, swap));
            break;
        case TypeCode::Int64:
            tensor->dataAs<int64_t>()[i] =
                h.itemSize == 8 ? readElement<int64_t>(src, swap)
                                : static_cast<int64_t>(readElement<int32_t>(src, swap));
            break;
        default:
            tensor->dataAs<bool_t>()[i] = std::to_integer<uint8_t>(*src) != 0 ? 1 : 0;
            break;
        }
    }
    return tensor;
}

void saveNpy(const TensorObject *tensor, const std::filesystem::path &path) {
    static_assert(
        std::endian::native == std::endian::little,
        "saveNpy writes host-order little-endian data");
    std::string descr;
    switch (tensor->dtype()) {
    case TypeCode::Float32:
        descr = "<f4";
        break;
    case TypeCode::Int64:
        descr = "<i8";
        break;
    default:
        descr = "|b1";
        break;
    }
    std::string shape = "(";
    for (size_t i = 0; i < tensor->rank(); ++i) {
        shape += std::to_string(tensor->dim(i)) +
                 (tensor->rank() == 1 ? "," : (i + 1 < tensor->rank() ? ", " : ""));
    }
    shape += ")";
    std::string header =
        "{'descr': '" + descr + "', 'fortran_order': False, 'shape': " + shape + ", }";
    // Pad with spaces so magic + version + length + header + '\n' is a multiple of 64.
    const size_t unpadded = 6 + 2 + 2 + header.size() + 1;
    header.append((64 - unpadded % 64) % 64, ' ');
    header.push_back('\n');

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail(path, "cannot open file for writing");
    }
    out.write(kMagic, 6);
    const char version[2] = {1, 0};
    out.write(version, 2);
    const auto length = static_cast<uint16_t>(header.size());
    const char len[2] = {static_cast<char>(length & 0xFF), static_cast<char>(length >> 8)};
    out.write(len, 2);
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.write(
        reinterpret_cast<const char *>(tensor->rawData()),
        static_cast<std::streamsize>(tensor->byteSize()));
    if (!out) {
        fail(path, "write failed");
    }
}

} // namespace camel::tensor
