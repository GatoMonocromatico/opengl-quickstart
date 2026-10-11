#pragma once
// -----------------------------------------------------------------------------
// Utf8.h -- small UTF-8 helpers, no dependencies
// -----------------------------------------------------------------------------
// Offsets are always byte offsets into the string. Anything malformed is read
// as one U+FFFD per bad byte, so every function makes progress on any input.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace utf8
{

constexpr uint32_t kReplacement = 0xFFFD;

// Decodes the codepoint at s[i] (i < s.size()). len gets the bytes it spans.
inline uint32_t decode(std::string_view s, size_t i, size_t& len)
{
	const unsigned char c = static_cast<unsigned char>(s[i]);
	len = 1;
	if (c < 0x80) return c;

	int extra = 0;
	uint32_t cp = 0;
	uint32_t minimum = 0;
	if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; minimum = 0x80; }
	else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; minimum = 0x800; }
	else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; minimum = 0x10000; }
	else return kReplacement;

	if (i + extra >= s.size()) return kReplacement;
	for (int k = 1; k <= extra; k++)
	{
		const unsigned char cc = static_cast<unsigned char>(s[i + k]);
		if ((cc & 0xC0) != 0x80) return kReplacement;
		cp = (cp << 6) | (cc & 0x3F);
	}
	// overlong forms, surrogates and out of range values are not real text
	if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return kReplacement;
	len = static_cast<size_t>(extra) + 1;
	return cp;
}

inline bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Offset of the codepoint boundary after i (clamped to s.size()).
inline size_t next(std::string_view s, size_t i)
{
	if (i >= s.size()) return s.size();
	size_t len;
	decode(s, i, len);
	return i + len;
}

// Offset of the codepoint boundary before i (0 when there is none).
inline size_t prev(std::string_view s, size_t i)
{
	if (i == 0) return 0;
	if (i > s.size()) i = s.size();
	size_t j = i - 1;
	// walk back over at most 3 continuation bytes, then check the lead covers i
	size_t back = 0;
	while (j > 0 && back < 3 && isContinuation(s[j])) { j--; back++; }
	size_t len;
	decode(s, j, len);
	if (j + len == i) return j;
	return i - 1; // malformed run: step one byte
}

// Snaps any offset down to a codepoint boundary.
inline size_t snap(std::string_view s, size_t i)
{
	if (i >= s.size()) return s.size();
	size_t j = i;
	size_t back = 0;
	while (j > 0 && back < 3 && isContinuation(s[j])) { j--; back++; }
	size_t len;
	decode(s, j, len);
	return (j + len > i) ? j : i;
}

inline size_t count(std::string_view s)
{
	size_t n = 0;
	for (size_t i = 0; i < s.size(); i = next(s, i)) n++;
	return n;
}

inline bool valid(std::string_view s)
{
	for (size_t i = 0; i < s.size();)
	{
		size_t len;
		const uint32_t cp = decode(s, i, len);
		if (cp == kReplacement && !(len == 3 && s.substr(i, 3) == "\xEF\xBF\xBD")) return false;
		i += len;
	}
	return true;
}

inline void append(std::string& out, uint32_t cp)
{
	if (cp < 0x80) out += static_cast<char>(cp);
	else if (cp < 0x800)
	{
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

// Copy of s where every malformed byte is replaced with U+FFFD.
inline std::string sanitize(std::string_view s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();)
	{
		size_t len;
		const uint32_t cp = decode(s, i, len);
		if (cp == kReplacement) append(out, kReplacement);
		else out.append(s.substr(i, len));
		i += len;
	}
	return out;
}

}
