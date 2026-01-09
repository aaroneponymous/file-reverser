

/**
 * @file LinearAllocator.h
 * @brief Overview of the memory system and the LinearAllocator interface.
 *
 * The memory system manages allocation and deallocation of instances through
 * the macros `ME_NEW` and `ME_DELETE`. All memory operations are performed
 * through specialized memory arenas, which handle allocation, freeing, and
 * proper construction of instances or arrays.
 *
 * Memory arenas also provide debugging features such as bounds checking
 * and memory tagging, which can be configured using policy settings.
 *
 * Internally, a memory arena requests raw memory from an allocator. Each
 * allocator follows a minimal, policy-agnostic interface that deals only with
 * raw memory allocation and release, without knowledge of higher-level
 * features such as tracking or debugging.
 *
 * In summary:
 * - Instances are managed via `ME_NEW` and `ME_DELETE`.
 * - Memory arenas orchestrate construction, destruction, and debugging.
 * - Allocators provide a simple, low-level interface for raw memory.
 *
 * The `LinearAllocator` implements this interface as shown below:
 *
 * @code
 * class LinearAllocator
 * {
 *  public:
 *    explicit LinearAllocator(size_t size);
 *    LinearAllocator(void* start, void* end);
 * 
 *    void* Allocate(size_t size, size_t alignment, size_t offset);
 * 
 *    inline void Free(void* ptr);
 * 
 *    inline void Reset(void);
 * 
 *  private:
 *    char* m_start;
 *    char* m_end;
 *    char* m_current;
 *  };
 * @endcode
 */


/**
 * @class LinearAllocator
 * @brief A simple, non-growing allocator optimized for sequential allocations.
 *
 * The `LinearAllocator` is designed for fast, linear memory allocation patterns
 * without the overhead of virtual calls or per-allocation tracking. It can be
 * used both as part of a memory arena or as a stand-alone allocator for raw
 * memory, such as per-frame GPU command buffers. This design minimizes
 * indirection, making allocation as simple as incrementing a pointer.
 *
 * ### Key Details
 * - `Allocate()` and `Free()` are **non-virtual**.  
 *   This avoids virtual function overhead and allows allocators to be used
 *   directly in performance-critical code paths.
 * - **Constructors:**  
 *   1. One constructor accepts a memory range `[start, end)`, which allows
 *      allocation from either stack memory (e.g., via `alloca()`) or heap-based
 *      memory (e.g., from `VirtualAlloc()`).
 *   2. The other constructor uses an internal page allocator to request
 *      physical memory for a given size.
 * - The allocator’s memory region **cannot grow**, ensuring predictable
 *   performance and no reallocation overhead.
 * - Individual allocations **cannot be freed**.  
 *   `Free()` is effectively empty, and `Reset()` clears all allocations by
 *   resetting the internal pointer (`m_current`) to the start.
 *
 * ### The Offset Parameter
 * The `Allocate()` method includes an `offset` parameter, which allows the
 * allocator to ensure alignment for user memory even when the arena needs to
 * prepend book-keeping or boundary-checking bytes.
 *
 * For example:
 * - Without bounds checking:
 *   `linearAllocator.Allocate(120, 16, 0);`
 * - With 4-byte bounds on each side:
 *   `linearAllocator.Allocate(128, 16, 4);`
 *
 * This ensures that the memory returned (after the 4-byte border) remains
 * correctly aligned to a 16-byte boundary. The offset-based alignment avoids
 * broken alignments and minimizes wasted space.
 *
 * ### Implementation
 * The allocation process offsets the current pointer, aligns it, and then
 * subtracts the same offset to return a properly aligned address:
 *
 * @code
 * void* LinearAllocator::Allocate(size_t size, size_t alignment, size_t offset)
 * {
 *   // Offset pointer first, align it, and offset it back
 *   m_current = pointerUtil::AlignTop(m_current + offset, alignment) - offset;
 * 
 *   void* userPtr = m_current;
 *   m_current += size;
 * 
 *   if (m_current >= m_end)
 *   {
 *     // Out of memory
 *     return nullptr;
 *   }
 * 
 *   return userPtr;
 * }
 * @endcode
 *
 * This simple approach achieves correct alignment with minimal overhead and
 * no wasted memory, while maintaining deterministic behavior.
 */


#include <cstdint>
#include <cstddef>
#include <memory>
#include <cassert>
#include <new>
#include <algorithm>


/** @revisit: std:max - max_align_v and this */
#if defined(__cpp_lib_hardware_interference_size)
    static constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
    static constexpr std::size_t cache_line_size = 64;
#endif

namespace memory_mgr
{

    template <std::size_t Align>
    struct AlignedDeleter
    {
        void operator()(void* ptr) const noexcept {
            ::operator delete[](ptr, std::align_val_t{ Align });
        }
    };

    template <typename T = std::byte, typename Deleter = AlignedDeleter<cache_line_size>>
    class LinearAllocator : private Deleter
    {
        using value_type = T;
        using pointer = T*;
        using unique_pointer = std::unique_ptr<T[], Deleter>;

        public:
            explicit LinearAllocator(std::size_t size, std::size_t align)
            {
                pointer raw = static_cast<pointer>(::operator new[]( size, std::align_val_t{ align } ));
                m_start_ = unique_pointer(raw, Deleter{ });  /** @revisit: redundant to pass Deleter here */
                m_end_ = raw + size;
                m_curr_ = raw;
            }

            /** @revisit: don't need it now */
            // LinearAllocator(void* start, void* end);

            LinearAllocator(const LinearAllocator&) = delete;
            LinearAllocator(LinearAllocator&&) = delete;
            LinearAllocator& operator=(const LinearAllocator&) = delete;
            LinearAllocator& operator=(LinearAllocator&&) = delete;

            [[nodiscard]] pointer allocate(std::size_t size) noexcept
            {
                auto bytes_left = std::distance(m_curr_, m_end_);
                if (bytes_left <= 0)  return nullptr;
                
                pointer m_user = m_curr_;
                m_curr_ += size;
                return m_user;
            }
            
            inline void reset(void) noexcept { m_curr_ = m_start_.get(); }
        
        private:
            unique_pointer m_start_;
            pointer m_end_;
            pointer m_curr_;
    };
}

