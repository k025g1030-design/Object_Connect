#include "ObjectConnect/Text/Utf8.hpp"

#include <cstdint>

namespace object_connect {
namespace {

constexpr char32_t kReplacementCharacter = U'\uFFFD';

struct SequenceDescription final {
    std::size_t length = 0;
    std::uint8_t payloadMask = 0;
    std::uint8_t secondMinimum = 0x80u;
    std::uint8_t secondMaximum = 0xBFu;
};

[[nodiscard]] SequenceDescription DescribeSequence(const std::uint8_t lead) noexcept {
    if (lead >= 0xC2u && lead <= 0xDFu) {
        return {2, 0x1Fu, 0x80u, 0xBFu};
    }
    if (lead == 0xE0u) {
        return {3, 0x0Fu, 0xA0u, 0xBFu};
    }
    if (lead >= 0xE1u && lead <= 0xECu) {
        return {3, 0x0Fu, 0x80u, 0xBFu};
    }
    if (lead == 0xEDu) {
        return {3, 0x0Fu, 0x80u, 0x9Fu};
    }
    if (lead >= 0xEEu && lead <= 0xEFu) {
        return {3, 0x0Fu, 0x80u, 0xBFu};
    }
    if (lead == 0xF0u) {
        return {4, 0x07u, 0x90u, 0xBFu};
    }
    if (lead >= 0xF1u && lead <= 0xF3u) {
        return {4, 0x07u, 0x80u, 0xBFu};
    }
    if (lead == 0xF4u) {
        return {4, 0x07u, 0x80u, 0x8Fu};
    }
    return {};
}

[[nodiscard]] bool IsContinuation(const std::uint8_t byte) noexcept {
    return byte >= 0x80u && byte <= 0xBFu;
}

void AppendReplacement(Utf8DecodeResult& result) {
    result.codePoints.push_back(kReplacementCharacter);
    ++result.replacementCount;
}

} // namespace

Utf8DecodeResult DecodeUtf8(const std::string_view text) {
    Utf8DecodeResult result;
    result.codePoints.reserve(text.size());

    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<std::uint8_t>(text[index]);
        if (lead <= 0x7Fu) {
            result.codePoints.push_back(static_cast<char32_t>(lead));
            ++index;
            continue;
        }

        const SequenceDescription sequence = DescribeSequence(lead);
        if (sequence.length == 0) {
            AppendReplacement(result);
            ++index;
            continue;
        }

        std::size_t prefixLength = 1;
        bool malformed = false;
        for (std::size_t offset = 1; offset < sequence.length; ++offset) {
            if (index + offset >= text.size()) {
                AppendReplacement(result);
                index += prefixLength;
                malformed = true;
                break;
            }

            const auto byte = static_cast<std::uint8_t>(text[index + offset]);
            if (!IsContinuation(byte)) {
                AppendReplacement(result);
                index += prefixLength;
                malformed = true;
                break;
            }
            if (offset == 1 &&
                (byte < sequence.secondMinimum || byte > sequence.secondMaximum)) {
                AppendReplacement(result);
                index += prefixLength;
                malformed = true;
                break;
            }
            ++prefixLength;
        }
        if (malformed) {
            continue;
        }

        char32_t codePoint = static_cast<char32_t>(lead & sequence.payloadMask);
        for (std::size_t offset = 1; offset < sequence.length; ++offset) {
            const auto byte = static_cast<std::uint8_t>(text[index + offset]);
            codePoint = static_cast<char32_t>((codePoint << 6u) | (byte & 0x3Fu));
        }
        result.codePoints.push_back(codePoint);
        index += sequence.length;
    }

    return result;
}

} // namespace object_connect
