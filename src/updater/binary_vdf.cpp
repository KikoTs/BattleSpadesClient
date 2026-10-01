#include "battlespades/updater/binary_vdf.hpp"

#include "battlespades/updater/text_vdf.hpp"

namespace battlespades::updater {
namespace {

constexpr std::size_t maximum_depth = 32U;

[[nodiscard]] std::optional<std::size_t> payload_size(BinaryVdfType type) noexcept {
    switch (type) {
    case BinaryVdfType::int32:
    case BinaryVdfType::float32:
    case BinaryVdfType::pointer:
    case BinaryVdfType::color: return 4U;
    case BinaryVdfType::uint64:
    case BinaryVdfType::int64: return 8U;
    default: return std::nullopt;
    }
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_{bytes} {}

    bool read_list(std::vector<BinaryVdfNode>& out, std::size_t depth, std::string& error) {
        if (depth > maximum_depth) {
            error = "binary VDF nests too deeply";
            return false;
        }
        for (;;) {
            if (pos_ >= bytes_.size()) {
                error = "binary VDF ends without a terminator";
                return false;
            }
            const auto tag = static_cast<BinaryVdfType>(bytes_[pos_++]);
            if (tag == BinaryVdfType::end) return true;
            BinaryVdfNode node;
            node.type = tag;
            if (!read_cstring(node.key, error)) return false;
            if (tag == BinaryVdfType::map) {
                if (!read_list(node.children, depth + 1U, error)) return false;
            } else if (tag == BinaryVdfType::string) {
                if (!read_cstring(node.text, error)) return false;
            } else if (const auto size = payload_size(tag); size.has_value()) {
                if (bytes_.size() - pos_ < *size) {
                    error = "binary VDF value '" + node.key + "' is truncated";
                    return false;
                }
                node.raw.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                bytes_.begin() + static_cast<std::ptrdiff_t>(pos_ + *size));
                pos_ += *size;
            } else {
                error = "unsupported binary VDF type " +
                        std::to_string(static_cast<unsigned>(tag)) + " at byte " +
                        std::to_string(pos_ - 1U);
                return false;
            }
            out.push_back(std::move(node));
        }
    }

    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

private:
    bool read_cstring(std::string& out, std::string& error) {
        const auto start = pos_;
        while (pos_ < bytes_.size() && bytes_[pos_] != 0U) ++pos_;
        if (pos_ >= bytes_.size()) {
            error = "unterminated binary VDF string at byte " + std::to_string(start);
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes_.data() + start), pos_ - start);
        ++pos_;
        return true;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_{};
};

void write_list(const std::vector<BinaryVdfNode>& nodes, std::vector<std::uint8_t>& out) {
    for (const auto& node : nodes) {
        out.push_back(static_cast<std::uint8_t>(node.type));
        out.insert(out.end(), node.key.begin(), node.key.end());
        out.push_back(0U);
        if (node.type == BinaryVdfType::map) {
            write_list(node.children, out);
        } else if (node.type == BinaryVdfType::string) {
            out.insert(out.end(), node.text.begin(), node.text.end());
            out.push_back(0U);
        } else {
            out.insert(out.end(), node.raw.begin(), node.raw.end());
        }
    }
    out.push_back(static_cast<std::uint8_t>(BinaryVdfType::end));
}

} // namespace

BinaryVdfNode BinaryVdfNode::make_map(std::string key) {
    BinaryVdfNode node;
    node.type = BinaryVdfType::map;
    node.key = std::move(key);
    return node;
}

BinaryVdfNode BinaryVdfNode::make_string(std::string key, std::string value) {
    BinaryVdfNode node;
    node.type = BinaryVdfType::string;
    node.key = std::move(key);
    node.text = std::move(value);
    return node;
}

BinaryVdfNode BinaryVdfNode::make_int32(std::string key, std::uint32_t value) {
    BinaryVdfNode node;
    node.type = BinaryVdfType::int32;
    node.key = std::move(key);
    node.raw = {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U),
                static_cast<std::uint8_t>(value >> 16U), static_cast<std::uint8_t>(value >> 24U)};
    return node;
}

std::optional<std::uint32_t> BinaryVdfNode::as_uint32() const noexcept {
    if (type != BinaryVdfType::int32 || raw.size() != 4U) return std::nullopt;
    return static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8U) |
           (static_cast<std::uint32_t>(raw[2]) << 16U) | (static_cast<std::uint32_t>(raw[3]) << 24U);
}

BinaryVdfNode* BinaryVdfNode::child(std::string_view name) {
    for (auto& node : children) {
        if (vdf_key_equals(node.key, name)) return &node;
    }
    return nullptr;
}

const BinaryVdfNode* BinaryVdfNode::child(std::string_view name) const {
    for (const auto& node : children) {
        if (vdf_key_equals(node.key, name)) return &node;
    }
    return nullptr;
}

std::optional<std::vector<BinaryVdfNode>> parse_binary_vdf(std::span<const std::uint8_t> bytes,
                                                           std::string& error) {
    Reader reader{bytes};
    std::vector<BinaryVdfNode> roots;
    if (!reader.read_list(roots, 0U, error)) return std::nullopt;
    if (reader.position() != bytes.size()) {
        error = "unexpected data after the binary VDF terminator";
        return std::nullopt;
    }
    return roots;
}

std::vector<std::uint8_t> serialize_binary_vdf(const std::vector<BinaryVdfNode>& roots) {
    std::vector<std::uint8_t> out;
    write_list(roots, out);
    return out;
}

} // namespace battlespades::updater
