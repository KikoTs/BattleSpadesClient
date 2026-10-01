#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/**
 * One key of a Valve text KeyValues ("VDF") document.
 *
 * Offsets index the document text so that edits can be spliced in place:
 * changing one Steam setting must leave every other byte of a 300 KB
 * localconfig.vdf untouched.
 */
struct TextVdfNode {
    std::string key;
    bool is_object{};
    std::string value;           ///< Unescaped value when !is_object.
    std::size_t key_begin{};     ///< First byte of the key token (its quote).
    std::size_t value_begin{};   ///< First byte of the value token or '{'.
    std::size_t value_end{};     ///< One past the value token or '}'.
    std::size_t close_brace{};   ///< Offset of '}' for objects.
    std::vector<TextVdfNode> children;
};

using VdfPath = std::vector<std::string>;

class TextVdfDocument {
public:
    [[nodiscard]] static std::optional<TextVdfDocument> parse(std::string text, std::string& error);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] const std::vector<TextVdfNode>& roots() const noexcept { return roots_; }

    /// Case-insensitive lookup, like Steam's own KeyValues.
    [[nodiscard]] const TextVdfNode* find(const VdfPath& path) const;
    [[nodiscard]] std::optional<std::string> get_string(const VdfPath& path) const;

    /**
     * Set a string value, creating missing parent objects. Existing values
     * are replaced in place; new keys are inserted before the parent's '}'
     * with the file's own indentation and line endings.
     */
    bool set_string(const VdfPath& path, std::string_view value, std::string& error);
    /// Remove one key (string or object). Returns false with no error when absent.
    bool remove(const VdfPath& path, std::string& error);

private:
    bool reparse(std::string& error);

    std::string text_;
    std::vector<TextVdfNode> roots_;
};

[[nodiscard]] std::string escape_vdf_string(std::string_view text);
[[nodiscard]] std::string unescape_vdf_string(std::string_view raw);
[[nodiscard]] bool vdf_key_equals(std::string_view left, std::string_view right) noexcept;

} // namespace battlespades::updater
