// Behaviour-level models of the .NET facilities the SF3 translation wizard relies on: string
// trimming, UTF-8 round trips, the culture-aware comparison used to break ties, List<T>.Sort's
// introsort (not stable) and NTFS directory order. Port of sfiii-native tools/sf3patch/dotnet.py
// (MIT). Strings are code point sequences (std::u32string), like Python's str.
//
// Case mapping and canonical decomposition cover ASCII, Latin-1, Latin Extended-A, Greek and
// Cyrillic; patch and file names are ASCII in every known package. Names outside those ranges
// compare by code point, which can order differently from the Python model (warned, not fatal).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf3::dotnet {

using U32 = std::u32string;

U32 utf8_decode(const uint8_t* p, size_t n);   // invalid sequences -> U+FFFD (maximal subparts)
inline U32 utf8_decode(const std::string& s) { return utf8_decode(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
std::string utf8_encode(const U32& s);         // lone surrogates -> '?' (Python errors="replace")

bool is_white_space(char32_t c);               // char.IsWhiteSpace
U32 trim(const U32& s);                        // string.Trim()
U32 trim_number(const U32& s);                 // NumberStyles leading/trailing white
U32 python_strip(const U32& s);                // Python str.strip()

size_t utf16_len(const U32& s);
U32 utf16_sub(const U32& s, size_t length);
U32 pad_right(const U32& s, size_t width, char32_t ch = U' ');

struct FormatError : std::runtime_error { using std::runtime_error::runtime_error; };
int32_t parse_hex_int(const U32& text);        // int.Parse(text, NumberStyles.HexNumber)
uint8_t parse_hex_byte(const U32& text);       // byte.Parse(text, NumberStyles.HexNumber)

char32_t simple_upper(char32_t c);
char32_t simple_lower(char32_t c);
U32 upper(const U32& s);
U32 lower(const U32& s);
bool outside_tables(const U32& s);             // true if any character is outside the case tables

// Sign of a.CompareTo(b) under an ICU-like en-US collation.
int culture_compare(const U32& a, const U32& b);

std::vector<uint32_t> ntfs_key(const U32& name);

// .NET 5 ArraySortHelper<T>.IntrospectiveSort, exactly. cmp(a, b) returns <0, 0 or >0.
template <class T, class C>
void introsort(std::vector<T>& a, C cmp);

// ---------------------------------------------------------------- introsort implementation
namespace detail {
template <class T, class C> void swap_if_greater(std::vector<T>& a, C& cmp, size_t i, size_t j) {
    if (cmp(a[i], a[j]) > 0) std::swap(a[i], a[j]);
}
template <class T, class C> void insertion(std::vector<T>& a, size_t lo, size_t size, C& cmp) {
    for (size_t i = 0; i + 1 < size; i++) {
        T t = a[lo + i + 1];
        long j = long(i);
        while (j >= 0 && cmp(t, a[lo + j]) < 0) { a[lo + j + 1] = a[lo + j]; j--; }
        a[lo + j + 1] = t;
    }
}
template <class T, class C> void down_heap(std::vector<T>& a, size_t lo, size_t i, size_t n, C& cmp) {
    T d = a[lo + i - 1];
    while (i <= n >> 1) {
        size_t child = 2 * i;
        if (child < n && cmp(a[lo + child - 1], a[lo + child]) < 0) child++;
        if (!(cmp(d, a[lo + child - 1]) < 0)) break;
        a[lo + i - 1] = a[lo + child - 1];
        i = child;
    }
    a[lo + i - 1] = d;
}
template <class T, class C> void heapsort(std::vector<T>& a, size_t lo, size_t n, C& cmp) {
    for (size_t i = n >> 1; i >= 1; i--) down_heap(a, lo, i, n, cmp);
    for (size_t i = n; i > 1; i--) {
        std::swap(a[lo], a[lo + i - 1]);
        down_heap(a, lo, 1, i - 1, cmp);
    }
}
template <class T, class C> size_t partition(std::vector<T>& a, size_t lo, size_t size, C& cmp) {
    size_t hi = size - 1, mid = hi >> 1;
    swap_if_greater(a, cmp, lo, lo + mid);
    swap_if_greater(a, cmp, lo, lo + hi);
    swap_if_greater(a, cmp, lo + mid, lo + hi);
    T pivot = a[lo + mid];
    std::swap(a[lo + mid], a[lo + hi - 1]);
    long left = 0, right = long(hi) - 1;
    while (left < right) {
        left++;
        while (cmp(a[lo + left], pivot) < 0) left++;
        right--;
        while (cmp(pivot, a[lo + right]) < 0) right--;
        if (left >= right) break;
        std::swap(a[lo + left], a[lo + right]);
    }
    if (size_t(left) != hi - 1) std::swap(a[lo + left], a[lo + hi - 1]);
    return size_t(left);
}
template <class T, class C> void intro(std::vector<T>& a, size_t lo, size_t size, int depth, C& cmp) {
    while (size > 1) {
        if (size <= 16) {
            if (size == 2) { swap_if_greater(a, cmp, lo, lo + 1); return; }
            if (size == 3) {
                swap_if_greater(a, cmp, lo, lo + 1);
                swap_if_greater(a, cmp, lo, lo + 2);
                swap_if_greater(a, cmp, lo + 1, lo + 2);
                return;
            }
            insertion(a, lo, size, cmp);
            return;
        }
        if (depth == 0) { heapsort(a, lo, size, cmp); return; }
        depth--;
        size_t p = partition(a, lo, size, cmp);
        intro(a, lo + p + 1, size - (p + 1), depth, cmp);
        size = p;
    }
}
}  // namespace detail

template <class T, class C>
void introsort(std::vector<T>& a, C cmp) {
    size_t n = a.size();
    if (n > 1) {
        int bl = 0;
        for (size_t x = n; x; x >>= 1) bl++;
        detail::intro(a, 0, n, 2 * (bl - 1 + 1), cmp);
    }
}

}  // namespace sf3::dotnet
