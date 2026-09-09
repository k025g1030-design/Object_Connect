#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace object_connect {

struct Utf8DecodeResult final {
    std::vector<char32_t> codePoints;
    std::size_t replacementCount = 0;
};

// Decodes UTF-8 into Unicode scalar values. Each maximal subpart of an
// ill-formed sequence is replaced with U+FFFD so decoding always makes
// forward progress. Line-ending code points are preserved verbatim.
[[nodiscard]] Utf8DecodeResult DecodeUtf8(std::string_view text);

} // namespace object_connect
