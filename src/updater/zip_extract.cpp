#include "battlespades/updater/zip_extract.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/steam_library.hpp"

#include <array>
#include <fstream>

namespace battlespades::updater {
namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t maximum_member_size = 2ULL * 1024ULL * 1024ULL * 1024ULL;

class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data) : data_{data} {}

    bool bits(int count, std::uint32_t& value) {
        while (available_ < count) {
            if (position_ >= data_.size()) return false;
            buffer_ |= static_cast<std::uint64_t>(data_[position_++]) << available_;
            available_ += 8;
        }
        value = static_cast<std::uint32_t>(buffer_ & ((1ULL << count) - 1ULL));
        buffer_ >>= count;
        available_ -= count;
        return true;
    }

    void align() {
        const int drop = available_ % 8;
        buffer_ >>= drop;
        available_ -= drop;
    }

    /// Byte-aligned read used by stored blocks (after align()).
    bool byte(std::uint8_t& value) {
        std::uint32_t bits_value{};
        if (!bits(8, bits_value)) return false;
        value = static_cast<std::uint8_t>(bits_value);
        return true;
    }

private:
    std::span<const std::uint8_t> data_;
    std::size_t position_{};
    std::uint64_t buffer_{};
    int available_{};
};

struct Huffman {
    std::array<std::uint16_t, 16> count{};
    std::array<std::uint16_t, 320> symbol{};
};

/// Canonical code construction; returns false for an over-subscribed set.
bool build(Huffman& huffman, const std::uint8_t* lengths, int n) {
    huffman.count.fill(0U);
    for (int i = 0; i < n; ++i) ++huffman.count[lengths[i]];
    if (huffman.count[0] == n) return true;   // no codes (allowed for distances)
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= huffman.count[static_cast<std::size_t>(len)];
        if (left < 0) return false;
    }
    std::array<std::uint16_t, 16> offsets{};
    for (int len = 1; len < 15; ++len) {
        offsets[static_cast<std::size_t>(len + 1)] =
            static_cast<std::uint16_t>(offsets[static_cast<std::size_t>(len)] + huffman.count[static_cast<std::size_t>(len)]);
    }
    for (int symbol = 0; symbol < n; ++symbol) {
        if (lengths[symbol] != 0U) {
            huffman.symbol[offsets[lengths[symbol]]++] = static_cast<std::uint16_t>(symbol);
        }
    }
    return true;
}

bool decode(BitReader& reader, const Huffman& huffman, int& symbol) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len < 16; ++len) {
        std::uint32_t bit{};
        if (!reader.bits(1, bit)) return false;
        code |= static_cast<int>(bit);
        const int count = huffman.count[static_cast<std::size_t>(len)];
        if (code - count < first) {
            symbol = huffman.symbol[static_cast<std::size_t>(index + (code - first))];
            return true;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return false;
}

constexpr std::array<std::uint16_t, 29> length_base{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> distance_base{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                      33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> distance_extra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_codes(BitReader& reader, const Huffman& lengths, const Huffman& distances,
                   std::vector<std::uint8_t>& out, std::uint64_t limit, std::string& error) {
    for (;;) {
        int symbol{};
        if (!decode(reader, lengths, symbol)) {
            error = "corrupt deflate data (literal/length code)";
            return false;
        }
        if (symbol < 256) {
            if (out.size() >= limit) {
                error = "deflate data is larger than declared";
                return false;
            }
            out.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        if (symbol == 256) return true;
        symbol -= 257;
        if (symbol >= 29) {
            error = "corrupt deflate data (length symbol)";
            return false;
        }
        std::uint32_t extra{};
        if (!reader.bits(length_extra[static_cast<std::size_t>(symbol)], extra)) {
            error = "truncated deflate data";
            return false;
        }
        const std::size_t length = length_base[static_cast<std::size_t>(symbol)] + extra;
        int distance_symbol{};
        if (!decode(reader, distances, distance_symbol) || distance_symbol >= 30) {
            error = "corrupt deflate data (distance code)";
            return false;
        }
        if (!reader.bits(distance_extra[static_cast<std::size_t>(distance_symbol)], extra)) {
            error = "truncated deflate data";
            return false;
        }
        const std::size_t distance = distance_base[static_cast<std::size_t>(distance_symbol)] + extra;
        if (distance > out.size()) {
            error = "corrupt deflate data (distance too far back)";
            return false;
        }
        if (out.size() + length > limit) {
            error = "deflate data is larger than declared";
            return false;
        }
        const std::size_t start = out.size() - distance;
        for (std::size_t i = 0; i < length; ++i) out.push_back(out[start + i]);
    }
}

bool fixed_tables(Huffman& lengths, Huffman& distances) {
    std::array<std::uint8_t, 320> code_lengths{};
    int symbol = 0;
    for (; symbol < 144; ++symbol) code_lengths[static_cast<std::size_t>(symbol)] = 8;
    for (; symbol < 256; ++symbol) code_lengths[static_cast<std::size_t>(symbol)] = 9;
    for (; symbol < 280; ++symbol) code_lengths[static_cast<std::size_t>(symbol)] = 7;
    for (; symbol < 288; ++symbol) code_lengths[static_cast<std::size_t>(symbol)] = 8;
    if (!build(lengths, code_lengths.data(), 288)) return false;
    for (symbol = 0; symbol < 30; ++symbol) code_lengths[static_cast<std::size_t>(symbol)] = 5;
    return build(distances, code_lengths.data(), 30);
}

bool dynamic_tables(BitReader& reader, Huffman& lengths, Huffman& distances, std::string& error) {
    static constexpr std::array<std::uint8_t, 19> order{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::uint32_t hlit{}, hdist{}, hclen{};
    if (!reader.bits(5, hlit) || !reader.bits(5, hdist) || !reader.bits(4, hclen)) {
        error = "truncated deflate header";
        return false;
    }
    const int literal_count = static_cast<int>(hlit) + 257;
    const int distance_count = static_cast<int>(hdist) + 1;
    if (literal_count > 286 || distance_count > 30) {
        error = "corrupt deflate header";
        return false;
    }
    std::array<std::uint8_t, 320> code_lengths{};
    for (std::uint32_t i = 0; i < hclen + 4U; ++i) {
        std::uint32_t value{};
        if (!reader.bits(3, value)) {
            error = "truncated deflate header";
            return false;
        }
        code_lengths[order[i]] = static_cast<std::uint8_t>(value);
    }
    Huffman code_code;
    if (!build(code_code, code_lengths.data(), 19)) {
        error = "corrupt deflate code-length code";
        return false;
    }
    code_lengths.fill(0U);
    int index = 0;
    while (index < literal_count + distance_count) {
        int symbol{};
        if (!decode(reader, code_code, symbol)) {
            error = "corrupt deflate code lengths";
            return false;
        }
        if (symbol < 16) {
            code_lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(symbol);
            continue;
        }
        std::uint8_t repeat_value = 0;
        std::uint32_t repeat{};
        if (symbol == 16) {
            if (index == 0) {
                error = "corrupt deflate code lengths (repeat without previous)";
                return false;
            }
            repeat_value = code_lengths[static_cast<std::size_t>(index - 1)];
            if (!reader.bits(2, repeat)) return false;
            repeat += 3U;
        } else if (symbol == 17) {
            if (!reader.bits(3, repeat)) return false;
            repeat += 3U;
        } else {
            if (!reader.bits(7, repeat)) return false;
            repeat += 11U;
        }
        if (index + static_cast<int>(repeat) > literal_count + distance_count) {
            error = "corrupt deflate code lengths (too many)";
            return false;
        }
        while (repeat-- > 0U) code_lengths[static_cast<std::size_t>(index++)] = repeat_value;
    }
    if (code_lengths[256] == 0U) {
        error = "corrupt deflate data (no end-of-block code)";
        return false;
    }
    if (!build(lengths, code_lengths.data(), literal_count) ||
        !build(distances, code_lengths.data() + literal_count, distance_count)) {
        error = "corrupt deflate Huffman tables";
        return false;
    }
    return true;
}

[[nodiscard]] std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
}

[[nodiscard]] std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

struct Member {
    std::string name;   // UTF-8, '/' separated
    std::uint16_t method{};
    std::uint32_t crc{};
    std::uint64_t compressed{};
    std::uint64_t size{};
    std::uint64_t local_offset{};
    bool directory{};
};

/// Rejects names that could escape the destination.
[[nodiscard]] bool safe_member(std::string& name) {
    for (auto& c : name) {
        if (c == '\\') c = '/';
    }
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) return false;
    std::size_t start{};
    while (start < name.size()) {
        auto end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        const auto part = std::string_view{name}.substr(start, end - start);
        if (part == "..") return false;
        start = end + 1U;
    }
    return true;
}

/// CP437 is ASCII-compatible; non-ASCII names without the UTF-8 flag are
/// rare (old tools) and are mapped through the upper half table.
[[nodiscard]] std::string cp437_to_utf8(std::string_view raw) {
    static constexpr char16_t upper[128] = {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE,
        0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6,
        0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA,
        0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502,
        0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514,
        0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
        0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C,
        0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
        0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320,
        0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
    std::string out;
    for (const char raw_char : raw) {
        const auto byte = static_cast<unsigned char>(raw_char);
        if (byte < 0x80U) {
            out.push_back(raw_char);
            continue;
        }
        const char16_t c = upper[byte - 0x80U];
        if (c < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (c >> 6U)));
            out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xE0U | (c >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
        }
    }
    return out;
}

bool read_at(std::ifstream& input, std::uint64_t offset, std::size_t count, std::vector<std::uint8_t>& out) {
    out.resize(count);
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) return false;
    input.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(count));
    return static_cast<std::size_t>(input.gcount()) == count;
}

bool central_directory(std::ifstream& input, std::uint64_t file_size, std::vector<Member>& members, std::string& error) {
    // End of central directory: the last 22..(22 + 65535) bytes.
    const std::uint64_t tail = std::min<std::uint64_t>(file_size, 22U + 65535U);
    std::vector<std::uint8_t> buffer;
    if (tail < 22U || !read_at(input, file_size - tail, static_cast<std::size_t>(tail), buffer)) {
        error = "not a ZIP archive (too small)";
        return false;
    }
    std::size_t eocd = std::string::npos;
    for (std::size_t i = buffer.size() - 22U + 1U; i-- > 0U;) {
        if (le32(&buffer[i]) == 0x06054b50U) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "not a ZIP archive (no end-of-central-directory record)";
        return false;
    }
    const std::uint16_t entries = le16(&buffer[eocd + 10U]);
    const std::uint32_t directory_size = le32(&buffer[eocd + 12U]);
    const std::uint32_t directory_offset = le32(&buffer[eocd + 16U]);
    if (entries == 0xFFFFU || directory_offset == 0xFFFFFFFFU) {
        error = "ZIP64 archives are not supported (keep packages below 4 GB and 65535 files)";
        return false;
    }
    if (static_cast<std::uint64_t>(directory_offset) + directory_size > file_size) {
        error = "corrupt ZIP central directory";
        return false;
    }
    std::vector<std::uint8_t> directory;
    if (!read_at(input, directory_offset, directory_size, directory)) {
        error = "cannot read the ZIP central directory";
        return false;
    }
    std::size_t p = 0;
    for (std::uint16_t index = 0; index < entries; ++index) {
        if (p + 46U > directory.size() || le32(&directory[p]) != 0x02014b50U) {
            error = "corrupt ZIP central directory entry";
            return false;
        }
        Member member;
        const std::uint16_t creator = le16(&directory[p + 4U]);
        const std::uint16_t flags = le16(&directory[p + 8U]);
        member.method = le16(&directory[p + 10U]);
        member.crc = le32(&directory[p + 16U]);
        member.compressed = le32(&directory[p + 20U]);
        member.size = le32(&directory[p + 24U]);
        const std::uint16_t name_length = le16(&directory[p + 28U]);
        const std::uint16_t extra_length = le16(&directory[p + 30U]);
        const std::uint16_t comment_length = le16(&directory[p + 32U]);
        const std::uint32_t external = le32(&directory[p + 38U]);
        member.local_offset = le32(&directory[p + 42U]);
        if (p + 46U + name_length + extra_length + comment_length > directory.size()) {
            error = "corrupt ZIP central directory entry";
            return false;
        }
        if ((flags & 0x0001U) != 0U) {
            error = "encrypted ZIP members are not supported";
            return false;
        }
        if (member.compressed == 0xFFFFFFFFU || member.size == 0xFFFFFFFFU || member.local_offset == 0xFFFFFFFFU) {
            error = "ZIP64 members are not supported";
            return false;
        }
        const std::string_view raw{reinterpret_cast<const char*>(&directory[p + 46U]), name_length};
        member.name = (flags & 0x0800U) != 0U ? std::string{raw} : cp437_to_utf8(raw);
        // Unix creator with S_IFLNK: refuse links outright.
        if ((creator >> 8U) == 3U && ((external >> 16U) & 0170000U) == 0120000U) {
            error = "ZIP member is a symbolic link: " + member.name;
            return false;
        }
        member.directory = !member.name.empty() && (member.name.back() == '/' || member.name.back() == '\\');
        if (!safe_member(member.name)) {
            error = "ZIP member escapes the destination: " + member.name;
            return false;
        }
        if (member.size > maximum_member_size) {
            error = "ZIP member is too large: " + member.name;
            return false;
        }
        members.push_back(std::move(member));
        p += 46U + name_length + extra_length + comment_length;
    }
    return true;
}

} // namespace

bool inflate_raw(std::span<const std::uint8_t> input, std::vector<std::uint8_t>& output, std::uint64_t expected_size,
                 std::string& error) {
    output.clear();
    output.reserve(static_cast<std::size_t>(expected_size));
    BitReader reader{input};
    for (;;) {
        std::uint32_t last{}, type{};
        if (!reader.bits(1, last) || !reader.bits(2, type)) {
            error = "truncated deflate data";
            return false;
        }
        if (type == 0U) {
            reader.align();
            std::uint8_t bytes[4]{};
            for (auto& byte : bytes) {
                if (!reader.byte(byte)) {
                    error = "truncated stored block";
                    return false;
                }
            }
            const std::uint16_t length = static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8U));
            const std::uint16_t complement = static_cast<std::uint16_t>(bytes[2] | (bytes[3] << 8U));
            if (length != static_cast<std::uint16_t>(~complement)) {
                error = "corrupt stored block";
                return false;
            }
            if (output.size() + length > expected_size) {
                error = "deflate data is larger than declared";
                return false;
            }
            for (std::uint16_t i = 0; i < length; ++i) {
                std::uint8_t byte{};
                if (!reader.byte(byte)) {
                    error = "truncated stored block";
                    return false;
                }
                output.push_back(byte);
            }
        } else if (type == 1U || type == 2U) {
            Huffman lengths, distances;
            if (type == 1U) {
                fixed_tables(lengths, distances);
            } else if (!dynamic_tables(reader, lengths, distances, error)) {
                return false;
            }
            if (!inflate_codes(reader, lengths, distances, output, expected_size, error)) return false;
        } else {
            error = "invalid deflate block type";
            return false;
        }
        if (last != 0U) break;
    }
    if (output.size() != expected_size) {
        error = "deflate data is shorter than declared";
        return false;
    }
    return true;
}

bool extract_zip_archive(const fs::path& archive, const fs::path& destination, std::string& error,
                         const ExtractProgress& progress) {
    std::ifstream input{archive, std::ios::binary};
    if (!input) {
        error = "cannot open " + path_to_utf8(archive);
        return false;
    }
    std::error_code code;
    const auto file_size = fs::file_size(archive, code);
    if (code) {
        error = "cannot read the size of " + path_to_utf8(archive);
        return false;
    }
    std::vector<Member> members;
    if (!central_directory(input, file_size, members, error)) return false;
    std::uint64_t total{};
    for (const auto& member : members) total += member.size;

    fs::create_directories(destination, code);
    std::uint64_t done{};
    std::vector<std::uint8_t> header, compressed, data;
    for (const auto& member : members) {
        const auto target = destination / path_from_utf8(member.name);
        if (member.directory) {
            fs::create_directories(target, code);
            continue;
        }
        if (!read_at(input, member.local_offset, 30U, header) || le32(header.data()) != 0x04034b50U) {
            error = "corrupt ZIP local header: " + member.name;
            return false;
        }
        const std::uint64_t data_offset = member.local_offset + 30U + le16(&header[26]) + le16(&header[28]);
        if (data_offset + member.compressed > file_size ||
            !read_at(input, data_offset, static_cast<std::size_t>(member.compressed), compressed)) {
            error = "truncated ZIP member: " + member.name;
            return false;
        }
        if (member.method == 0U) {
            if (member.compressed != member.size) {
                error = "corrupt stored ZIP member: " + member.name;
                return false;
            }
            data = compressed;
        } else if (member.method == 8U) {
            if (!inflate_raw(compressed, data, member.size, error)) {
                error = member.name + ": " + error;
                return false;
            }
        } else {
            error = "unsupported ZIP compression method " + std::to_string(member.method) + ": " + member.name;
            return false;
        }
        const std::string_view bytes{reinterpret_cast<const char*>(data.data()), data.size()};
        if (crc32(bytes) != member.crc) {
            error = "CRC mismatch in ZIP member: " + member.name;
            return false;
        }
        fs::create_directories(target.parent_path(), code);
        std::ofstream output{target, std::ios::binary | std::ios::trunc};
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            error = "cannot write " + path_to_utf8(target);
            return false;
        }
        done += member.size;
        if (progress && !progress(done, total)) {
            error = "cancelled";
            return false;
        }
    }
    return true;
}

} // namespace battlespades::updater
