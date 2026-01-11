#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <cstdint>
#include <type_traits>
#include <concepts>
#include <optional>

#ifdef __cpp_lib_hardware_interference_size
  #include <new>
  inline constexpr std::size_t cacheline_size =
      std::hardware_destructive_interference_size;
#else
  inline constexpr std::size_t cacheline_size = 64;
#endif



/** @revisit: what if we want to push Job struct (it is a non-aggregate class) 
 *            has a non-default ctor provided
*/
template <typename T>
class SPSCLockFreeQ
{
    static_assert(std::is_trivially_copyable_v<T>,
            "This SPSCLockFreeQ assumes trivially copyable T.");

    using value_type = T;
    using size_type = std::uint8_t;
    using index_type = std::atomic<size_type>;
    static_assert(index_type::is_always_lock_free);
    using pointer = T*;


public:
    explicit SPSCLockFreeQ(pointer queue_buff, size_type capacity) 
        : q_buff_{ queue_buff }, cap_{ capacity }
    {
        if (!q_buff_) throw std::invalid_argument("queue buffer is null");
        if (cap_ < 2 || (cap_ & (cap_ - 1)) != 0)
            throw std::invalid_argument("capacity must be >= 2 and a power of 2");

        mask_ = static_cast<size_type>(cap_ - 1);
    }

    SPSCLockFreeQ(const SPSCLockFreeQ&) = delete;
    SPSCLockFreeQ(SPSCLockFreeQ&&) = delete;
    SPSCLockFreeQ& operator=(const SPSCLockFreeQ&) = delete;
    SPSCLockFreeQ& operator=(SPSCLockFreeQ&&) = delete;

    ~SPSCLockFreeQ() = default;

    bool push(const T& item) noexcept
    {
        const auto write = write_idx_.v.load(std::memory_order_relaxed);
        const auto write_next = static_cast<size_type>((write + 1) & mask_);

        if (write_next == read_idx_.v.load(std::memory_order_acquire)) return false;
        q_buff_[write] = item;
        write_idx_.v.store(write_next, std::memory_order_release);

        return true;
    }

    bool pop(T& item) noexcept
    {
        const auto read = read_idx_.v.load(std::memory_order_relaxed);
        if (read == write_idx_.v.load(std::memory_order_acquire)) return false;
        
        item = q_buff_[read];
        const auto read_next = static_cast<size_type>((read + 1) & mask_);
        read_idx_.v.store(read_next, std::memory_order_release);
        return true;
    }

    bool full() const noexcept
    {
        const auto write = write_idx_.v.load(std::memory_order_relaxed);
        const auto write_next = static_cast<size_type>((write + 1) & mask_);
        return write_next == read_idx_.v.load(std::memory_order_acquire);
    }

    bool empty() const noexcept { return size() == 0; }

private:
    std::size_t size() const noexcept
    {
        const auto write = write_idx_.v.load(std::memory_order_acquire);
        const auto read  = read_idx_.v.load(std::memory_order_acquire);
        return static_cast<std::size_t>((write - read) & mask_);
    }

    struct alignas(cacheline_size) PaddedIndex
    {
        index_type v{ };
        std::byte padding[cacheline_size - sizeof(index_type)]{ };
    };

    static_assert(sizeof(PaddedIndex) == cacheline_size);
    static_assert(alignof(PaddedIndex) == cacheline_size);
    static_assert(sizeof(index_type) <= cacheline_size);

    pointer q_buff_{ nullptr };
    size_type cap_{ };
    size_type mask_{ };

    std::byte padd[ cacheline_size - (sizeof(pointer) + (2 * sizeof(size_type))) ]{ };

    PaddedIndex write_idx_{ };
    PaddedIndex read_idx_{ }; 
};