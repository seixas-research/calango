#include "core/NumpyArray.hpp"

#include <algorithm>
// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively, so the build breaks there without it even though clangd on
// macOS reports it unused. Do not remove.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace calango::core {

namespace {

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

/// Value of `key` in a `.npy` header dict, as raw text between the delimiters
/// that follow it. The header is a Python literal, but it is written by
/// `numpy.lib.format` to a fixed shape, so a scan for the key and then for its
/// value is enough — and far less than embedding a Python parser.
std::string headerField(const std::string& header, const std::string& key,
                        char open, char close)
{
    const std::size_t at = header.find("'" + key + "'");
    if (at == std::string::npos)
        return {};
    // Past the key's own closing quote, or a quote-delimited search finds the
    // key itself and every dtype reads back as the string "descr".
    const std::size_t after = at + key.size() + 2;
    const std::size_t from = header.find(open, after);
    if (from == std::string::npos)
        return {};
    const std::size_t to = header.find(close, from + 1);
    if (to == std::string::npos)
        return {};
    return header.substr(from + 1, to - from - 1);
}

} // namespace

std::size_t NumpyArray::elementCount() const
{
    return values.size();
}

std::size_t NumpyArray::shapeProduct() const
{
    std::size_t total = 1;
    for (const std::size_t n : shape)
        total *= n;
    return shape.empty() ? 1 : total;
}

bool readNumpyArray(const std::string& path, NumpyArray& out,
                    std::string* error)
{
    if (error)
        error->clear();
    out = NumpyArray{};

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return fail(error, "Cannot open '" + path + "'.");

    char magic[8] = {};
    in.read(magic, 8);
    if (in.gcount() != 8 || std::memcmp(magic, "\x93NUMPY", 6) != 0)
        return fail(error, "'" + path + "' is not a .npy file.");

    const unsigned major = static_cast<unsigned char>(magic[6]);
    std::size_t headerLength = 0;
    if (major == 1) {
        unsigned char raw[2] = {};
        in.read(reinterpret_cast<char*>(raw), 2);
        if (in.gcount() != 2)
            return fail(error, "Truncated .npy header length.");
        headerLength = static_cast<std::size_t>(raw[0])
            | (static_cast<std::size_t>(raw[1]) << 8);
    } else if (major == 2 || major == 3) {
        unsigned char raw[4] = {};
        in.read(reinterpret_cast<char*>(raw), 4);
        if (in.gcount() != 4)
            return fail(error, "Truncated .npy header length.");
        headerLength = static_cast<std::size_t>(raw[0])
            | (static_cast<std::size_t>(raw[1]) << 8)
            | (static_cast<std::size_t>(raw[2]) << 16)
            | (static_cast<std::size_t>(raw[3]) << 24);
    } else {
        return fail(error, "Unsupported .npy format version "
                        + std::to_string(major) + ".");
    }

    std::string header(headerLength, '\0');
    in.read(header.data(), static_cast<std::streamsize>(headerLength));
    if (static_cast<std::size_t>(in.gcount()) != headerLength)
        return fail(error, "Truncated .npy header.");

    // -- dtype --------------------------------------------------------------
    const std::string descr = headerField(header, "descr", '\'', '\'');
    if (descr.empty())
        return fail(error, "No dtype in the .npy header.");
    if (!descr.empty() && descr.front() == '>')
        return fail(error, "Big-endian .npy arrays are not supported (dtype '"
                        + descr + "').");
    const std::string kind =
        (descr.front() == '<' || descr.front() == '=' || descr.front() == '|')
        ? descr.substr(1)
        : descr;
    std::size_t itemSize = 0;
    if (kind == "f8") {
        out.type = NumpyArray::Type::Float64;
        itemSize = 8;
    } else if (kind == "c16") {
        out.type = NumpyArray::Type::Complex128;
        itemSize = 16;
    } else {
        return fail(error, "Unsupported .npy dtype '" + descr
                        + "'; expected float64 or complex128. Save the array "
                          "with .astype(float) or .astype(complex) first.");
    }

    // -- order --------------------------------------------------------------
    // Fortran order would transpose every index silently, which is exactly the
    // failure that produces believable-but-wrong physics, so it is refused
    // rather than handled.
    const std::string order = headerField(header, "fortran_order", ' ', ',');
    if (order.find("True") != std::string::npos)
        return fail(error, "Fortran-ordered .npy arrays are not supported; "
                           "write with numpy.ascontiguousarray().");

    // -- shape --------------------------------------------------------------
    const std::string shapeText = headerField(header, "shape", '(', ')');
    std::size_t pos = 0;
    while (pos < shapeText.size()) {
        while (pos < shapeText.size()
               && (shapeText[pos] == ' ' || shapeText[pos] == ','))
            ++pos;
        std::size_t digits = pos;
        while (digits < shapeText.size() && shapeText[digits] >= '0'
               && shapeText[digits] <= '9')
            ++digits;
        if (digits == pos)
            break;
        out.shape.push_back(static_cast<std::size_t>(
            std::stoull(shapeText.substr(pos, digits - pos))));
        pos = digits;
    }

    const std::size_t count = out.shapeProduct();
    if (count == 0) {
        out.values.clear();
        return true;
    }

    // -- body ---------------------------------------------------------------
    // Read in blocks: the production array is tens of gigabytes and a
    // whole-file buffer on top of the destination would double that.
    out.values.resize(count);
    constexpr std::size_t kBlock = 1u << 16;
    std::vector<double> buffer(kBlock * (itemSize / 8));
    std::size_t done = 0;
    while (done < count) {
        const std::size_t chunk = std::min(kBlock, count - done);
        const std::streamsize bytes =
            static_cast<std::streamsize>(chunk * itemSize);
        in.read(reinterpret_cast<char*>(buffer.data()), bytes);
        if (in.gcount() != bytes)
            return fail(error, "'" + path
                            + "' ends early: expected "
                            + std::to_string(count)
                            + " elements from its declared shape.");
        if (out.type == NumpyArray::Type::Float64) {
            for (std::size_t i = 0; i < chunk; ++i)
                out.values[done + i] = buffer[i];
        } else {
            for (std::size_t i = 0; i < chunk; ++i) {
                const double re = buffer[2 * i];
                const double im = buffer[2 * i + 1];
                out.values[done + i] = re * re + im * im;
            }
        }
        done += chunk;
    }
    return true;
}

} // namespace calango::core
