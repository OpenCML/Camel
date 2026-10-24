// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `.npy` reader and writer (see npy.h). The header is a Python dict literal;
 * only the three keys the format defines ('descr', 'fortran_order', 'shape')
 * are parsed.
 */

#include "npy.h"

#include "camel/utils/path.h"
#include "binary.h"
#include "dtype.h"

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
    throw std::invalid_argument("npy '" + camel::utils::pathToUtf8(path) + "': " + what);
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
    result.byteOrder = descr[1];
    result.kind      = descr[2];
    size_t sizeEnd   = 0;
    result.itemSize  = static_cast<size_t>(std::stoul(descr.substr(3), &sizeEnd));
    if (3 + sizeEnd >= descr.size() || descr[3 + sizeEnd] != '\'')
        fail(path, "malformed dtype size");
    const auto order = field(header, "fortran_order", path);
    size_t orderEnd  = order.starts_with("True") ? 4 : order.starts_with("False") ? 5 : 0;
    if (!orderEnd)
        fail(path, "malformed Fortran order flag");
    result.fortran = orderEnd == 4;
    while (orderEnd < order.size() && order[orderEnd] == ' ')
        ++orderEnd;
    if (orderEnd == order.size() || (order[orderEnd] != ',' && order[orderEnd] != '}'))
        fail(path, "malformed Fortran order flag");
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

Array *loadNpy(
    const std::filesystem::path &path, mm::IAllocator &allocator,
    std::optional<TypeCode> declaredElement) {
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
    if (h.kind == 'f' && h.itemSize == 4) {
        dtype = TypeCode::Float32;
    } else if (h.kind == 'f' && h.itemSize == 8) {
        dtype = TypeCode::Float64;
    } else if (h.kind == 'i' && h.itemSize == 4) {
        dtype = TypeCode::Int32;
    } else if (h.kind == 'i' && h.itemSize == 8) {
        dtype = TypeCode::Int64;
    } else if (h.kind == 'b' && h.itemSize == 1) {
        dtype = TypeCode::Bool;
    } else {
        fail(
            path,
            std::string("unsupported element type '") + h.kind + std::to_string(h.itemSize) + "'");
    }

    // The file's element type is the tensor's: `load_npy` never converts (design principles
    // 5.1; design/arrays-memory plan 5.2). An f8 file is an f64 tensor, an i4 file an i32 one.
    if (declaredElement.has_value()) {
        // What the program declared for this load is checked here, where the file's own element
        // type becomes known: a `.npy` file's dtype is decided by whoever wrote it, so the one
        // thing that cannot be decided when the program is compiled is checked when it runs, at
        // the call (the fault carries its position), and never repaired by a conversion
        // (design principles 5.5: the runtime has no type information; handover H466).
        const TypeCode declared = *declaredElement;
        if (!isSupportedTensorScalar(declared)) {
            fail(
                path,
                "the declared element type '" + elementTypeSpelling(declared) +
                    "' is not one a .npy file holds");
        }
        if (dtype != declared) {
            fail(
                path, "the file holds " + elementTypeSpelling(dtype) + " and the program declares " +
                          elementTypeSpelling(declared));
        }
    }
    Array *tensor = createTensor(dtype, h.shape, allocator);
    const uint64_t count = tensor->numel();
    std::vector<std::byte> raw(count * h.itemSize);
    in.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (static_cast<size_t>(in.gcount()) != raw.size()) {
        fail(path, "truncated data");
    }
    dispatchDType(dtype, [&]<typename T>() {
        T *dst = tensor->dataAs<T>();
        for (uint64_t i = 0; i < count; ++i) {
            dst[i] = readElement<T>(raw.data() + i * h.itemSize, swap);
        }
    });
    return tensor;
}

std::string encodeNpy(TypeCode dtype, std::span<const int64_t> dims, std::string_view payload) {
    if (payload.size() != tensorPayloadSize(dtype, dims)) {
        throw ShapeError("NPY payload length does not match its storage dtype and shape");
    }
    std::string descr;
    switch (dtype) {
    case TypeCode::Float32:
        descr = "<f4";
        break;
    case TypeCode::Float64:
        descr = "<f8";
        break;
    case TypeCode::Int32:
        descr = "<i4";
        break;
    case TypeCode::Int64:
        descr = "<i8";
        break;
    case TypeCode::Bool:
        descr = "|b1";
        break;
    default:
        throw std::invalid_argument("unsupported NPY storage dtype");
    }
    std::string shape = "(";
    for (size_t i = 0; i < dims.size(); ++i) {
        shape +=
            std::to_string(dims[i]) + (dims.size() == 1 ? "," : (i + 1 < dims.size() ? ", " : ""));
    }
    shape += ")";
    std::string header =
        "{'descr': '" + descr + "', 'fortran_order': False, 'shape': " + shape + ", }";
    // Pad with spaces so magic + version + length + header + '\n' is a multiple of 64.
    const size_t unpadded = 6 + 2 + 2 + header.size() + 1;
    header.append((64 - unpadded % 64) % 64, ' ');
    header.push_back('\n');

    if (header.size() > UINT16_MAX)
        throw std::invalid_argument("NPY v1 header exceeds 65535 bytes");
    std::string result(kMagic, 6);
    result.append("\1\0", 2);
    const auto length = static_cast<uint16_t>(header.size());
    result.push_back(static_cast<char>(length & 0xff));
    result.push_back(static_cast<char>(length >> 8));
    result += header;
    result.append(payload);
    return result;
}

NpyStorageView inspectNpyExact(std::string_view bytes) {
    const std::filesystem::path source("<checkpoint>");
    if (bytes.size() < 10 || bytes.substr(0, 6) != std::string_view(kMagic, 6))
        fail(source, "invalid magic");
    const auto major = static_cast<unsigned char>(bytes[6]);
    if (bytes[7] != 0 || major < 1 || major > 3)
        fail(source, "unsupported format version");
    const size_t lengthSize = major == 1 ? 2 : 4;
    if (bytes.size() < 8 + lengthSize)
        fail(source, "truncated header length");
    size_t headerSize = 0;
    for (size_t i = 0; i < lengthSize; ++i)
        headerSize |= size_t(static_cast<unsigned char>(bytes[8 + i])) << (8 * i);
    if (headerSize > 1024 * 1024 || headerSize > bytes.size() - 8 - lengthSize)
        fail(source, "invalid header length");
    const auto parsed = parseHeader(std::string(bytes.substr(8 + lengthSize, headerSize)), source);
    if (parsed.fortran)
        fail(source, "Fortran order is not a checkpoint storage layout");
    TypeCode dtype;
    if (parsed.kind == 'f' && parsed.itemSize == 4)
        dtype = TypeCode::Float32;
    else if (parsed.kind == 'i' && parsed.itemSize == 8)
        dtype = TypeCode::Int64;
    else if (parsed.kind == 'b' && parsed.itemSize == 1)
        dtype = TypeCode::Bool;
    else
        fail(source, "checkpoint requires an exact f32/int64/bool storage dtype");
    if (parsed.byteOrder != '<' && parsed.byteOrder != '>' && parsed.byteOrder != '=' &&
        !(parsed.byteOrder == '|' && parsed.itemSize == 1))
        fail(source, "invalid byte order");
    const auto payload = bytes.substr(8 + lengthSize + headerSize);
    if (payload.size() != tensorPayloadSize(dtype, parsed.shape))
        fail(source, "payload length differs from shape");
    if (dtype == TypeCode::Bool &&
        !std::all_of(payload.begin(), payload.end(), [](unsigned char b) { return b <= 1; }))
        fail(source, "bool payload must contain only zero or one");
    const bool big = parsed.byteOrder == '>' ||
                     (parsed.byteOrder == '=' && std::endian::native == std::endian::big);
    return {dtype, parsed.shape, payload, big};
}

Array *decodeNpyExact(std::string_view bytes, mm::IAllocator &allocator) {
    const auto parsed = inspectNpyExact(bytes);
    std::string payload(parsed.payload);
    const size_t itemSize = parsed.dtype == TypeCode::Int64     ? 8
                            : parsed.dtype == TypeCode::Float32 ? 4
                                                                : 1;
    if (parsed.bigEndian)
        for (size_t i = 0; i < payload.size(); i += itemSize) {
            std::reverse(payload.begin() + i, payload.begin() + i + itemSize);
        }
    return tensorFromLittleEndian(payload, parsed.dtype, parsed.shape, allocator);
}

void saveNpy(const Array *tensor, const std::filesystem::path &path) {
    const auto encoded =
        encodeNpy(tensor->dtype(), tensor->shapeSpan(), tensorToLittleEndian(tensor));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail(path, "cannot open file for writing");
    }
    out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    if (!out) {
        fail(path, "write failed");
    }
}

} // namespace camel::tensor
