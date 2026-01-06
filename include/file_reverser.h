#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <span>
#include <algorithm>
#include <ranges>
#include <iostream>
#include <atomic>
#include <new>

#ifdef __cpp_lib_hardware_interference_size
  #include <new>
  inline constexpr std::size_t L1_SIZE =
      std::hardware_destructive_interference_size;
#else
  inline constexpr std::size_t L1_SIZE = 64;
#endif



namespace file_reverser
{


/****************************** Buffer, Segments, and Reversal Logic ******************************/

    /**  
     *  @buffer:
     *  - A buffer is encapsulated within the Segment struct
     * 
     *  @segment:
     *  - struct Segment holds valid bytes in the buffer (len_), 
     *    that are either:
     *    1. Read In, 2. To Be Processed, or 3. Written To
     *    from a specified offset (off_)
     * 
     *  @writeitem:
     *  - struct WriteItem organizes and holds the components
     *    needed to perform a write to file
     * 
     *  - @members:
     *    - seg_ array can hold upto two Segment objects:
     * 
     *    - if a carrying logic is needed, a carry buffer
     *      Segment object always resides at seg_[0]
     *      followed by a normal buffer segment
     * 
     *      - if a carry buffer is non-empty, then it
     *        holds reversed bytes (trailing bytes from previous
     *        iteration + a prefix ending with '\n' or
     *        without if EOF) and thus those bytes always 
     *        come before the recently processed buffer
     *        containing bytes ahead of the prefix
    */

    struct Segment
    {
        std::byte* buff_{ nullptr };
        std::size_t len_{ };
        std::size_t off_{ };
    };

    struct WriteItem
    {
        Segment seg_[2];                   // size: 2 * 24 : 48 bytes
        std::int8_t seg_count_{ };         // size: 1 byte - padding of 7 bytes

        WriteItem() = default;
        WriteItem(const Segment& a, const Segment& b) : seg_count_{ 2 }
        {
            seg_[0] = a;
            seg_[1] = b;
        }
    };

    namespace utilities
    {
        constexpr inline uint8_t u8(std::byte b) noexcept { return std::to_integer<uint8_t>(b); }

        /** @utf8 multi-byte: Pattern has at least two
         *   beginning bits set to 1
         * 
         * - Continuation Byte: 10xx xxxx 
         *   - 0xC0u : 1100 0000 
         *   - 0x80u : 1000 0000
         */

        constexpr inline bool is_cont(std::byte b) noexcept { return (u8(b) & 0xC0u) == 0x80u; }

        // valid UTF-8 leading byte range (excludes C0,C1, F5..FF)
        constexpr inline bool is_lead(std::byte b) noexcept 
        {
            uint8_t x = u8(b);
            return x >= 0xC2u && x <= 0xF4u;
        }

        /** @reversing: Two-Pass
         *  1st Pass: reverse the bytes in the span                : linear (wrt n)
         *  2nd Pass: for each byte that is a continuation         : linear 4 x k  - where 4 is the total bytes for the worst case 
         *                                                                           multi-byte character and k, the amount of multi-byte 
         *                                                                           characters that appear in the line
         *            byte, find its corresponding leading
         *            byte and reverse them (multi-byte character)
         */

        // reverse UTF-8 code points inside [from, to) (to exclude '\n') 
        constexpr inline bool reverse_range(std::span<std::byte> buf, std::size_t from, std::size_t to) noexcept
        {
            if (to <= from) return true;    // 1 or 0 bytes 
            
            std::ranges::reverse(buf.subspan(from, to - from)); // first pass
            std::size_t i = from;
            while (i < to)          // second pass
            {
                if (!is_cont(buf[i])) // ASCII (<0x80) or a lead byte already (should be rare after step 1)
                {
                    ++i;
                    continue;
                }

                const std::size_t start = i;
                while (i < to && is_cont(buf[i])) ++i;
                if (i >= to || !is_lead(buf[i])) return false; // malformed UTF-8 in this range (or chunk split a code point).

                const std::size_t len = (i - start) + 1; // continuations + lead
                std::ranges::reverse(buf.subspan(start, len));
                i = start + len;
            }

            return true;
        }
    }

    namespace utilities::st
    {

        inline void handle_carry(std::byte* lf, Segment& seg_recent, Segment& carry, Segment& carry_backup, WriteItem& item_to_write) 
        {
            std::size_t prefix_size = (lf - seg_recent.buff_) + 1; // copy '\n' as well
            std::memcpy( carry.buff_ + carry.len_, seg_recent.buff_, prefix_size );
            carry.len_ += prefix_size;
            carry.off_ = 0;

            assert(lf >= seg_recent.buff_);
            assert(carry.len_ >= 1);
            assert(carry.buff_[carry.len_ - 1] == std::byte{0x0A});

            // safety guard around len_-1 (prevents underflow if assumptions ever break)
            std::size_t to = (carry.len_ >= 1) ? (carry.len_ - 1) : 0;  // one before the end of byte array at '\n' - gets excluded in reverse_range

            if (to > 0 && carry.buff_[to - 1] == std::byte{0x0D}) --to;     // 0x0D - carriage return
            std::span<std::byte> carry_span{ carry.buff_, carry.len_ };

            // only reverse if there is content before EOL 
            // (reverse_range already handles to<=from; explicit safety)
            if (to > 0) reverse_range(carry_span, 0, to);

            auto& index_seg = item_to_write.seg_count_;
            item_to_write.seg_[index_seg++] = carry;        // carry copied into item to write - safe to reset the len_ and offset_ and swap with carry_backup next
            carry.len_ = carry.off_ = 0;
            std::swap(carry, carry_backup);

            seg_recent.len_ -= prefix_size;
            seg_recent.off_ = prefix_size;
        }

        inline void handle_carry_eof(Segment& seg_recent, Segment& carry, Segment& carry_backup, WriteItem& item_to_write) 
        {
            std::memcpy(carry.buff_ + carry.len_, seg_recent.buff_, seg_recent.len_);
            carry.len_ += seg_recent.len_;
            seg_recent.off_ = 0;
            seg_recent.len_ = 0;
            
            std::size_t to = carry.len_;
            std::span<std::byte> carry_span{ carry.buff_, carry.len_ };

            if (to > 0) reverse_range(carry_span, 0, to);

            auto& index_seg = item_to_write.seg_count_;
            item_to_write.seg_[index_seg++] = carry;        // carry copied into item to write - safe to reset the len_ and offset_ and swap with carry_backup next
            carry.len_ = carry.off_ = 0;
            std::swap(carry, carry_backup);
        }

        inline void reverse_seg_recent(Segment& seg_recent, Segment& carry, Segment& carry_backup, WriteItem& item_to_write) 
        {

            std::span<std::byte> seg_recent_span{ seg_recent.buff_ + seg_recent.off_ , seg_recent.len_ };
            const std::byte* span_base_ptr = seg_recent_span.data();
            const std::size_t pos_end = seg_recent_span.size();
            std::size_t curr_pos{ };

            while (curr_pos < pos_end) 
            {
                auto* lf_next = static_cast<std::byte*>(
                    std::memchr(static_cast<void*>(&seg_recent_span[curr_pos]), '\n', pos_end - curr_pos)
                );

                if (!lf_next) 
                {
                    const std::size_t tail = pos_end - curr_pos;

                    // bytes to write are [seg_recent.off_, curr_pos)
                    seg_recent.len_ = curr_pos;

                    std::memcpy(carry.buff_, seg_recent_span.data() + curr_pos, tail);
                    carry.off_ = 0;
                    carry.len_ = tail;
                    break;
                }

                const std::size_t lf  = static_cast<std::size_t>(lf_next - span_base_ptr);

                // found newline must be within the current scan window
                assert(lf >= curr_pos && lf < pos_end);

                std::size_t end = lf; // excludes '\n'

                if (end > curr_pos && seg_recent_span[end - 1] == std::byte{0x0D}) --end;

                if (!reverse_range(seg_recent_span, curr_pos, end)) {
                    throw std::runtime_error("reverse_range false: malformed UTF-8 or code point split");
                }

                curr_pos = lf + 1;
            }


            auto& index_seg = item_to_write.seg_count_;
            item_to_write.seg_[index_seg++] = seg_recent;


        }

        inline WriteItem reverse_segment(Segment& seg_recent, Segment& carry, Segment& carry_backup)
        {
            WriteItem item_to_write{ };
            
            if (carry.len_ > 0)     // carry contains unprocessed trailing bytes from previous iteration
            {
                auto *lf = static_cast<std::byte*>(
                    std::memchr(seg_recent.buff_, '\n', seg_recent.len_)
                );

                /** @precondition: lf is only null if EOF (invariant max line with '\n' should be 4096 bytes) */
                if (!lf) 
                {
                    handle_carry_eof(seg_recent, carry, carry_backup, item_to_write);
                    return item_to_write;
                }
                
                handle_carry(lf, seg_recent, carry, carry_backup, item_to_write);
            }
           
            // reverse segment seg_recent and handle if there needs to be bytes carried
            // over into carry buffer
            reverse_seg_recent(seg_recent, carry, carry_backup, item_to_write);
            return item_to_write;
        }        
    
    }


    namespace utilities::mt
    {

    
    }


/****************************** Memory Management & Allocation, and SPSCQ ******************************/

    template <typename T, typename Allocator = std::allocator<T>>
    class SPSC_LFQ
    {
    public:
        static_assert(std::is_trivially_copyable_v<T>,
                    "This SPSC_LFQ assumes trivially copyable T.");

        using value_type      = T;
        using size_type       = std::uint8_t;
        using index_type      = std::atomic<size_type>;
        using allocator_type  = Allocator;
        using alloc_traits    = std::allocator_traits<allocator_type>;
        using pointer         = typename alloc_traits::pointer; // typically T*

        explicit SPSC_LFQ(size_type capacity, const allocator_type& alloc = allocator_type())
            : alloc_(alloc), cap_(capacity)
        {
            if (cap_ < 2 || (cap_ & (cap_ - 1)) != 0) {
                throw std::invalid_argument("capacity must be >= 2 and a power of 2");
            }
            mask_  = static_cast<size_type>(cap_ - 1);
            queue_ = alloc_traits::allocate(alloc_, static_cast<std::size_t>(cap_));
        }

        ~SPSC_LFQ() noexcept
        {
            if (queue_) {
                alloc_traits::deallocate(alloc_, queue_, static_cast<std::size_t>(cap_));
            }
        }

        SPSC_LFQ(const SPSC_LFQ&)            = delete;
        SPSC_LFQ& operator=(const SPSC_LFQ&) = delete;
        SPSC_LFQ(SPSC_LFQ&&)                 = delete;
        SPSC_LFQ& operator=(SPSC_LFQ&&)      = delete;

        bool push(const T& item) noexcept
        {
            const auto write = write_.load(std::memory_order_relaxed);
            const auto write_next = static_cast<size_type>((write + 1) & mask_);

            if (write_next == read_.load(std::memory_order_acquire)) return false;

            queue_[write] = item;
            write_.store(write_next, std::memory_order_release);
            return true;
        }

        template <class... Args>
        bool emplace_push(Args&&... args) noexcept
        {
            const auto write = write_.load(std::memory_order_relaxed);
            const auto write_next = static_cast<size_type>((write + 1) & mask_);

            if (write_next == read_.load(std::memory_order_acquire)) return false;

            queue_[write] = T{ std::forward<Args>(args)... };
            write_.store(write_next, std::memory_order_release);
            return true;
        }

        bool pop(T& item) noexcept
        {
            const auto read = read_.load(std::memory_order_relaxed);
            if (read == write_.load(std::memory_order_acquire)) return false;

            item = queue_[read];

            const auto read_next = static_cast<size_type>((read + 1) & mask_);
            read_.store(read_next, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool full() const noexcept
        {
            const auto write = write_.load(std::memory_order_relaxed);
            const auto write_next = static_cast<size_type>((write + 1) & mask_);
            return write_next == read_.load(std::memory_order_acquire);
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            const auto write = write_.load(std::memory_order_acquire);
            const auto read  = read_.load(std::memory_order_acquire);
            return static_cast<std::size_t>((write - read) & mask_);
        }

        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    private:
        allocator_type alloc_{};
        pointer        queue_{};
        size_type      cap_{};
        size_type      mask_{};

        alignas(L1_SIZE) index_type read_{0};
        alignas(L1_SIZE) index_type write_{0};
    };
}

    // template <typename T>
    // class SPSC_LFQ
    // {
    // public:
    //     static_assert(std::is_trivially_copyable_v<T>,
    //                 "This SPSC_LFQ assumes trivially copyable T (client provides T[cap]).");

    //     using value_type = T;
    //     using size_type  = std::uint8_t;
    //     using index_type = std::atomic<size_type>;
    //     using pointer = T*;

    //     explicit SPSC_LFQ(T* queue_buff, size_type capacity) :
    //             queue_{ queue_buff }, cap_{ capacity }
    //     {

    //         if (cap_ < 2 || (cap_ & (cap_ - 1)) != 0) {
    //             throw std::invalid_argument("capacity must be >= 2 and a power of 2");
    //         }
    //         mask_ = static_cast<size_type>(cap_ - 1);
    //     }

    //     bool push(const T& item) noexcept
    //     {
    //         auto write = write_.load(std::memory_order_relaxed);
    //         auto write_next = static_cast<size_type>((write + 1) & mask_);

    //         if (write_next == read_.load(std::memory_order_acquire)) return false;

    //         queue_[write] = item;
    //         write_.store(write_next, std::memory_order_release);
    //         return true;
    //     }

    //     template <class... Args>
    //     bool emplace_push(Args&&... args) noexcept
    //     {
    //         auto write = write_.load(std::memory_order_relaxed);
    //         auto write_next = static_cast<size_type>((write + 1) & mask_);

    //         if (write_next == read_.load(std::memory_order_acquire)) return false;

    //         queue_[write] = T{ std::forward<Args>(args)... };
    //         write_.store(write_next, std::memory_order_release);
    //         return true;
    //     }

    //     bool pop(T& item) noexcept
    //     {
    //         auto read = read_.load(std::memory_order_relaxed);
    //         if (read == write_.load(std::memory_order_acquire)) return false;

    //         item = queue_[read];

    //         auto read_next = static_cast<size_type>((read + 1) & mask_);
    //         read_.store(read_next, std::memory_order_release);
    //         return true;
    //     }

    //     [[nodiscard]] bool full() const noexcept
    //     {
    //         auto write = write_.load(std::memory_order_relaxed);
    //         auto write_next = static_cast<size_type>((write + 1) & mask_);
    //         return write_next == read_.load(std::memory_order_acquire);
    //     }

    //     [[nodiscard]] std::size_t size() const noexcept
    //     {
    //         auto write = write_.load(std::memory_order_acquire);
    //         auto read  = read_.load(std::memory_order_acquire);
    //         return static_cast<std::size_t>((write - read) & mask_);
    //     }

    //     [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // private:
    //     pointer   queue_{ };
    //     size_type cap_{ };
    //     size_type mask_{ };

    //     alignas(L1_SIZE) index_type read_{ 0 };
    //     alignas(L1_SIZE) index_type write_{ 0 };
    // };

    // template <typename T>
    // class SPSC_LFQ
    // {
    // public:
    //     static_assert(std::is_trivially_copyable_v<T>,
    //                 "This SPSC_LFQ assumes trivially copyable T (client provides T[cap]).");

    //     using value_type = T;
    //     using size_type  = std::uint8_t;
    //     using index_type = std::atomic<size_type>;

    //     using pointer = T*;

    //     explicit SPSC_LFQ(T* queue_buffer, size_type capacity)
    //         : queue_{ queue_buffer }
    //         , cap_{ capacity }
    //     {
    //         if (!queue_) {
    //             throw std::invalid_argument("queue_buffer must not be null");
    //         }
    //         if (cap_ < 2 || (cap_ & (cap_ - 1)) != 0) {
    //             throw std::invalid_argument("capacity must be >= 2 and a power of 2");
    //         }
    //         mask_ = static_cast<size_type>(cap_ - 1);
    //     }

    //     bool push(const T& item) noexcept
    //     {
    //         auto write = write_.load(std::memory_order_relaxed);
    //         auto write_next = static_cast<size_type>((write + 1) & mask_);

    //         if (write_next == read_.load(std::memory_order_acquire)) return false;

    //         queue_[write] = item;
    //         write_.store(write_next, std::memory_order_release);
    //         return true;
    //     }

    //     template <class... Args>
    //     bool emplace_push(Args&&... args) noexcept
    //     {
    //         auto write = write_.load(std::memory_order_relaxed);
    //         auto write_next = static_cast<size_type>((write + 1) & mask_);

    //         if (write_next == read_.load(std::memory_order_acquire)) return false;

    //         queue_[write] = T{ std::forward<Args>(args)... };
    //         write_.store(write_next, std::memory_order_release);
    //         return true;
    //     }

    //     bool pop(T& item) noexcept
    //     {
    //         auto read = read_.load(std::memory_order_relaxed);
    //         if (read == write_.load(std::memory_order_acquire)) return false;

    //         item = queue_[read];

    //         auto read_next = static_cast<size_type>((read + 1) & mask_);
    //         read_.store(read_next, std::memory_order_release);
    //         return true;
    //     }

    //     [[nodiscard]] bool full() const noexcept
    //     {
    //         auto write = write_.load(std::memory_order_relaxed);
    //         auto write_next = static_cast<size_type>((write + 1) & mask_);
    //         return write_next == read_.load(std::memory_order_acquire);
    //     }

    //     [[nodiscard]] std::size_t size() const noexcept
    //     {
    //         auto write = write_.load(std::memory_order_acquire);
    //         auto read  = read_.load(std::memory_order_acquire);
    //         return static_cast<std::size_t>((write - read) & mask_);
    //     }

    //     [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // private:
    //     pointer   queue_{ nullptr };
    //     size_type cap_{ 0 };
    //     size_type mask_{ 0 };

    //     alignas(L1_SIZE) index_type read_{ 0 };
    //     alignas(L1_SIZE) index_type write_{ 0 };
    // };


/****************************** Threads & Processing ******************************/
