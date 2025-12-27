#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <span>
#include <algorithm>
#include <ranges>
#include <iostream>


namespace file_reverser
{

    /** @components: */

    struct Segment
    {
        std::byte* buff_{ nullptr };
        std::size_t off_{ };
        std::size_t len_{ };
    };

    struct WriteItem
    {
        Segment seg_[2];
        std::int8_t seg_count_{ };         /** @revist: alignment-requirements  */
    };

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

    // either use relative positioning or absolute
    // absolute easier

    inline WriteItem reverse_segment(Segment& seg_in, Segment& carry_a, Segment& carry_b) noexcept
    {
        WriteItem item_to_write{ };

        const std::size_t seg_in_size = seg_in.len_;

        if (carry_a.len_ > 0)
        {
            char *prefix_lf  = static_cast<char*>(
                std::memchr(seg_in.buff_, '\n', seg_in.len_)
            );
            
            std::size_t prefix_size = (reinterpret_cast<std::byte*>(prefix_lf) - seg_in.buff_) + 1; // one past the '\n'
            std::memcpy( carry_a.buff_ + carry_a.len_, seg_in.buff_, prefix_size);

            carry_a.len_ += prefix_size;
            carry_a.off_ = 0;

            std::span<std::byte> carry_a_span{ carry_a.buff_, carry_a.len_ };
            const auto is_reversed = reverse_range(carry_a_span, carry_a.off_, carry_a.len_);
            auto& index_seg = item_to_write.seg_count_;
            item_to_write.seg_[index_seg++] = carry_a;
            // ++index_seg;

            // can we switch carry_a & carry_b now?
            carry_a.len_ = carry_a.off_ = 0;
            std::swap(carry_a, carry_b);

            seg_in.len_ -= prefix_size;   // size reduced
            seg_in.off_ = prefix_size;
        }

        std::size_t curr_pos = seg_in.off_;
        const std::size_t pos_end = seg_in.off_ + seg_in.len_;
        assert(pos_end == seg_in_size);
        std::span<std::byte> seg_in_span{ seg_in.buff_, seg_in_size };

        while (curr_pos < pos_end)
        {
            char* lf_next =  static_cast<char*>(
                std::memchr(seg_in.buff_ + curr_pos, '\n', pos_end - curr_pos)
            );

            if (!lf_next)
            {
                std::size_t tail = seg_in_size - curr_pos; // check again
                seg_in.len_ -= tail;

                std::memcpy( carry_a.buff_, seg_in.buff_ +
                            static_cast<ptrdiff_t>(curr_pos), tail);
                carry_a.len_ = tail;
                carry_a.off_ = 0;

                break;
            }

            std::size_t lf = reinterpret_cast<std::byte*>(lf_next) - seg_in.buff_;
            std::size_t end = lf;

            if (end > curr_pos && seg_in.buff_[end - 1] == std::byte{ 0x0D }) --end;

            if (!reverse_range(seg_in_span, curr_pos, end)) {
                std::cout << "total_size: " << seg_in_span.size() << "\n";
                std::cout << "curr_pos: " << curr_pos << " - " << 
                            static_cast<char>(seg_in_span[curr_pos]) << "\n";
                std::cout << "end: " << end << " - " << 
                            static_cast<char>(seg_in_span[end]) << "\n";
                std::cout << "string: " << reinterpret_cast<char*>((seg_in_span.subspan(curr_pos, seg_in_span.size() - curr_pos - 1)).data()) << "\n";
                
                throw std::runtime_error("reverse_range returned false\n");
            }

            curr_pos = lf + 1;
        }

        auto& seg_index = item_to_write.seg_count_;
        item_to_write.seg_[seg_index++] = seg_in;

        return std::move(item_to_write);
    } 

} 