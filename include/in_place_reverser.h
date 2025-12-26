#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <algorithm>
#include <ranges>
#include <iostream>


namespace utf8_inplace
{
    inline static uint8_t u8(std::byte b) noexcept { return std::to_integer<uint8_t>(b); }

    /** @utf8: Pattern
     * - Continuation Byte: 10xx xxxx 
     *   - 0xC0u : 1100 0000 
     *   - 0x80u : 1000 0000
     */

    inline static bool is_cont(std::byte b) noexcept { return (u8(b) & 0xC0u) == 0x80u; }

    // valid UTF-8 leading byte range (excludes C0,C1, F5..FF)
    inline bool is_lead(std::byte b) noexcept 
    {
        uint8_t x = u8(b);
        return x >= 0xC2u && x <= 0xF4u;
    }

    /** @reversing: Two-Pass with 1 Buffer
     * @todo: carry buffer
     * @todo: EOF
     */

    // reverse UTF-8 code points inside [from, to) (to excludes '\n')
    inline bool reverse_range(std::span<std::byte> buf, std::size_t from, std::size_t to) noexcept
    {
        if (to <= from) return true;

        // 1) reverse the raw bytes
        std::ranges::reverse(buf.subspan(from, to - from));
        // 2) repair multi-byte characters
        std::size_t i = from;
        while (i < to) 
        {
            if (!is_cont(buf[i])) { // ASCII (<0x80) or a lead byte already (should be rare after step 1)
                ++i;
                continue;
            }

            const std::size_t start = i;
            while (i < to && is_cont(buf[i])) ++i;

            if (i >= to || !is_lead(buf[i])) {
                // malformed UTF-8 in this range (or chunk split a code point).
                return false;
            }

            const std::size_t len = (i - start) + 1; // continuations + lead
            std::ranges::reverse(buf.subspan(start, len));
            i = start + len;
        }

        return true;
    }







    /** @note: buffer & carry_buffer switching mechanism (needs to be revisited) 
     *  @return: int? int32_t? - our max size of buffer would be 5 MiB
    */
    inline int reverse_place(std::byte*& buffer, std::size_t bytes_written,
                                       std::size_t buffer_size, std::byte*& carry_buffer, 
                                       std::size_t carry_size, std::size_t& carry_bytes) noexcept
    {
        const std::byte LF{ 0x0A };
        std::size_t pos = 0;
        std::span<std::byte> buf{ buffer, bytes_written };

        if (carry_bytes > 0)
        {
            const std::size_t remaining_cap = carry_size - carry_bytes;
            const std::size_t copy_range = (bytes_written > remaining_cap) ? remaining_cap : bytes_written;
            std::memcpy(carry_buffer + carry_bytes, buffer, copy_range);
            buf = std::span<std::byte>{ carry_buffer, carry_bytes + copy_range };
        }

        while (pos < buf.size()) 
        {
            auto it = std::find(buf.begin() + static_cast<std::ptrdiff_t>(pos), buf.end(), LF);

            if (it == buf.end()) 
            {
                auto had_carry = carry_bytes > 0;
                auto remaining_count = buf.size() - pos;
                auto will_carry = pos < buf.size();
                auto buf_has_new_bytes = bytes_written > 0;

                if (!buf_has_new_bytes && remaining_count > 0)
                {
                    bool reversed = reverse_range(buf, pos, buf.size());
                    std::swap(carry_buffer, buffer);
                    
                    return buf.size();
                }

                if (had_carry)
                {
                    std::memcpy(buffer, carry_buffer, remaining_count);
                    auto copy_range = buf.size() - carry_bytes;
                    std::memcpy(buffer + remaining_count, buffer + copy_range, bytes_written - copy_range);
                    carry_bytes = remaining_count + (bytes_written - copy_range);

                    std::swap(buffer, carry_buffer);
                    return pos + 1;

                }

                if (will_carry)
                {
                    std::memcpy(carry_buffer, buffer, remaining_count);
                    carry_bytes = remaining_count;
                }

               break; 

            }

            std::size_t lf = static_cast<std::size_t>(it - buf.begin());
            std::size_t end = lf;

            // handle CRLF by excluding '\r' from the reversal (keeps "\r\n" intact)
            if (end > pos && buf[end - 1] == std::byte{0x0D}) {
                --end;
            }

            if (!reverse_range(buf, pos, end)) {
                return -1;
            }

            pos = lf + 1; // move past '\n'
        }

        return pos;

    }

}