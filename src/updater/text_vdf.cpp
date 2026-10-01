#include "battlespades/updater/text_vdf.hpp"

#include <cctype>

namespace battlespades::updater {
namespace {

constexpr std::size_t maximum_depth = 64U;

enum class TokenKind { string, open, close, end };

struct Token {
    TokenKind kind{TokenKind::end};
    std::string text;
    std::size_t begin{};
    std::size_t end{};
};

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

class Tokenizer {
public:
    Tokenizer(std::string_view text, std::size_t start) : text_{text}, pos_{start} {}

    bool next(Token& token, std::string& error) {
        for (;;) {
            while (pos_ < text_.size() && is_space(text_[pos_])) ++pos_;
            if (pos_ + 1U < text_.size() && text_[pos_] == '/' && text_[pos_ + 1U] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            // Platform conditionals such as [$WIN32] carry no data for us.
            if (pos_ < text_.size() && text_[pos_] == '[') {
                const auto close = text_.find(']', pos_);
                if (close == std::string_view::npos) {
                    error = "unterminated conditional at byte " + std::to_string(pos_);
                    return false;
                }
                pos_ = close + 1U;
                continue;
            }
            break;
        }
        token = Token{};
        token.begin = pos_;
        if (pos_ >= text_.size()) {
            token.kind = TokenKind::end;
            token.end = pos_;
            return true;
        }
        const char c = text_[pos_];
        if (c == '{' || c == '}') {
            token.kind = c == '{' ? TokenKind::open : TokenKind::close;
            token.end = ++pos_;
            return true;
        }
        token.kind = TokenKind::string;
        if (c == '"') {
            const auto start = ++pos_;
            while (pos_ < text_.size() && text_[pos_] != '"') {
                if (text_[pos_] == '\\' && pos_ + 1U < text_.size()) ++pos_;
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                error = "unterminated string at byte " + std::to_string(token.begin);
                return false;
            }
            token.text = unescape_vdf_string(text_.substr(start, pos_ - start));
            token.end = ++pos_;
            return true;
        }
        while (pos_ < text_.size() && !is_space(text_[pos_]) && text_[pos_] != '{' &&
               text_[pos_] != '}' && text_[pos_] != '"') {
            ++pos_;
        }
        token.text = std::string{text_.substr(token.begin, pos_ - token.begin)};
        token.end = pos_;
        return true;
    }

private:
    std::string_view text_;
    std::size_t pos_{};
};

bool parse_list(Tokenizer& tokenizer, std::vector<TextVdfNode>& out, std::size_t depth,
                bool until_close, std::size_t& close_position, std::string& error) {
    if (depth > maximum_depth) {
        error = "document nests deeper than " + std::to_string(maximum_depth) + " levels";
        return false;
    }
    for (;;) {
        Token key;
        if (!tokenizer.next(key, error)) return false;
        if (key.kind == TokenKind::end) {
            if (until_close) {
                error = "missing '}' at end of document";
                return false;
            }
            return true;
        }
        if (key.kind == TokenKind::close) {
            if (!until_close) {
                error = "unexpected '}' at byte " + std::to_string(key.begin);
                return false;
            }
            close_position = key.begin;
            return true;
        }
        if (key.kind != TokenKind::string) {
            error = "expected a key at byte " + std::to_string(key.begin);
            return false;
        }
        Token value;
        if (!tokenizer.next(value, error)) return false;
        TextVdfNode node;
        node.key = std::move(key.text);
        node.key_begin = key.begin;
        node.value_begin = value.begin;
        if (value.kind == TokenKind::string) {
            node.value = std::move(value.text);
            node.value_end = value.end;
        } else if (value.kind == TokenKind::open) {
            node.is_object = true;
            std::size_t close{};
            if (!parse_list(tokenizer, node.children, depth + 1U, true, close, error)) return false;
            node.close_brace = close;
            node.value_end = close + 1U;
        } else {
            error = "key '" + node.key + "' has no value at byte " + std::to_string(value.begin);
            return false;
        }
        out.push_back(std::move(node));
    }
}

[[nodiscard]] const TextVdfNode* find_child(const std::vector<TextVdfNode>& nodes,
                                            std::string_view key) {
    for (const auto& node : nodes) {
        if (vdf_key_equals(node.key, key)) return &node;
    }
    return nullptr;
}

[[nodiscard]] std::string quoted(std::string_view text) {
    return '"' + escape_vdf_string(text) + '"';
}

[[nodiscard]] std::string build_block(const VdfPath& path, std::size_t first,
                                      std::string_view value, const std::string& indent,
                                      std::string_view newline) {
    std::string block = indent + quoted(path[first]);
    if (first + 1U == path.size()) {
        block += "\t\t" + quoted(value);
        block += newline;
        return block;
    }
    block += newline;
    block += indent + "{";
    block += newline;
    block += build_block(path, first + 1U, value, indent + "\t", newline);
    block += indent + "}";
    block += newline;
    return block;
}

} // namespace

bool vdf_key_equals(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

std::string escape_vdf_string(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 8U);
    for (const char c : text) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

std::string unescape_vdf_string(std::string_view raw) {
    std::string text;
    text.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1U < raw.size()) {
            const char next = raw[++i];
            switch (next) {
            case 'n': text.push_back('\n'); break;
            case 't': text.push_back('\t'); break;
            case '\\': text.push_back('\\'); break;
            case '"': text.push_back('"'); break;
            default:
                // Unknown escapes are kept verbatim, as Steam does.
                text.push_back('\\');
                text.push_back(next);
                break;
            }
        } else {
            text.push_back(raw[i]);
        }
    }
    return text;
}

std::optional<TextVdfDocument> TextVdfDocument::parse(std::string text, std::string& error) {
    TextVdfDocument document;
    document.text_ = std::move(text);
    if (!document.reparse(error)) return std::nullopt;
    return document;
}

bool TextVdfDocument::reparse(std::string& error) {
    roots_.clear();
    // A UTF-8 byte-order mark is tolerated but never produced.
    std::string_view view{text_};
    std::size_t skip{};
    if (view.size() >= 3U && static_cast<unsigned char>(view[0]) == 0xEFU &&
        static_cast<unsigned char>(view[1]) == 0xBBU && static_cast<unsigned char>(view[2]) == 0xBFU) {
        skip = 3U;
    }
    Tokenizer tokenizer{view, skip};
    std::size_t unused{};
    return parse_list(tokenizer, roots_, 0U, false, unused, error);
}

const TextVdfNode* TextVdfDocument::find(const VdfPath& path) const {
    const std::vector<TextVdfNode>* level = &roots_;
    const TextVdfNode* node = nullptr;
    for (const auto& key : path) {
        node = find_child(*level, key);
        if (node == nullptr) return nullptr;
        level = &node->children;
    }
    return node;
}

std::optional<std::string> TextVdfDocument::get_string(const VdfPath& path) const {
    const auto* node = find(path);
    if (node == nullptr || node->is_object) return std::nullopt;
    return node->value;
}

bool TextVdfDocument::set_string(const VdfPath& path, std::string_view value, std::string& error) {
    if (path.empty()) {
        error = "empty key path";
        return false;
    }
    const std::string newline = text_.find("\r\n") != std::string::npos ? "\r\n" : "\n";

    const std::vector<TextVdfNode>* level = &roots_;
    const TextVdfNode* parent = nullptr;
    std::size_t depth{};
    for (; depth < path.size(); ++depth) {
        const auto* child = find_child(*level, path[depth]);
        if (child == nullptr) break;
        if (depth + 1U == path.size()) {
            if (child->is_object) {
                error = "'" + path[depth] + "' is an object, not a value";
                return false;
            }
            text_.replace(child->value_begin, child->value_end - child->value_begin, quoted(value));
            return reparse(error);
        }
        if (!child->is_object) {
            error = "'" + path[depth] + "' is a value, not an object";
            return false;
        }
        parent = child;
        level = &child->children;
    }

    if (parent == nullptr) {
        std::string block = build_block(path, 0U, value, "", newline);
        if (!text_.empty() && text_.back() != '\n') block.insert(0U, newline);
        text_ += block;
        return reparse(error);
    }

    const auto close = parent->close_brace;
    std::size_t line_start = close;
    while (line_start > 0U && text_[line_start - 1U] != '\n') --line_start;
    bool brace_on_own_line = true;
    for (std::size_t i = line_start; i < close; ++i) {
        if (text_[i] != ' ' && text_[i] != '\t') {
            brace_on_own_line = false;
            break;
        }
    }
    if (brace_on_own_line) {
        const std::string parent_indent = text_.substr(line_start, close - line_start);
        text_.insert(line_start, build_block(path, depth, value, parent_indent + "\t", newline));
    } else {
        text_.insert(close, newline + build_block(path, depth, value, "\t", newline));
    }
    return reparse(error);
}

bool TextVdfDocument::remove(const VdfPath& path, std::string& error) {
    const auto* node = find(path);
    if (node == nullptr) {
        error.clear();
        return false;
    }
    std::size_t begin = node->key_begin;
    std::size_t line_start = begin;
    while (line_start > 0U && (text_[line_start - 1U] == ' ' || text_[line_start - 1U] == '\t')) {
        --line_start;
    }
    if (line_start == 0U || text_[line_start - 1U] == '\n') begin = line_start;
    std::size_t end = node->value_end;
    while (end < text_.size() && (text_[end] == ' ' || text_[end] == '\t')) ++end;
    if (end < text_.size() && text_[end] == '\r') ++end;
    if (end < text_.size() && text_[end] == '\n') ++end;
    text_.erase(begin, end - begin);
    return reparse(error);
}

} // namespace battlespades::updater
