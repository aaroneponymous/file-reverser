#include "../../include/file_reverser.hpp"
#include "../../include/io_raii.hpp"
#include "../../include/spsc_lockfree_q.hpp"
#include <string>
#include <memory>
#include <new>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <syncstream>
#include <vector>

#define sync_out std::osyncstream(std::cout)

using namespace memory_mgr;

struct Args
{
    std::size_t buffer_count{};
    std::size_t buffer_size{};
    std::size_t buff_in_flight{};
    std::size_t queue_count{};
    std::size_t queue_cap{};

    std::size_t buffer_stride{};
    std::size_t buff_arr_size{};
    std::size_t buff_arr_size_inflight{};
    std::size_t queue_size{};
    std::size_t queue_stride{};
    std::size_t queue_arr_size{};
    std::size_t total_size{};
    std::size_t cachel_size{};

    // argv layout: ./program x y <buffer_size> <buffer_count> <queue_count> <queue_cap>
    explicit Args(char* const argv[]) noexcept
        : buffer_size (static_cast<std::size_t>(std::atoi(argv[3])))
        , buffer_count(static_cast<std::size_t>(std::atoi(argv[4])))
        , buff_in_flight(buffer_count - 1)
        , queue_count (static_cast<std::size_t>(std::atoi(argv[5])))
        , queue_cap   (static_cast<std::size_t>(std::atoi(argv[6])))
        , buffer_stride(file_reverser::round_up(buffer_size, cache_line_size))
        , buff_arr_size(buffer_count * buffer_stride)
        , buff_arr_size_inflight(buff_in_flight * buffer_stride)
        , queue_size(sizeof(std::uint8_t) * queue_cap)
        , queue_stride(file_reverser::round_up(queue_size, cache_line_size))
        , queue_arr_size(queue_count * queue_stride)
        , total_size(buff_arr_size + queue_arr_size)
        , cachel_size(cache_line_size)
    {}
};



int main(int argc, char* argv[])
{
    std::string in_path(argv[1]);
    std::string out_path(argv[2]);

    Args test_args(argv);

    const std::size_t buffer_size{ test_args.buffer_size };
    const std::size_t buffer_count{ test_args.buffer_count };
    const std::size_t buffer_in_flight{ test_args.buff_in_flight };


    const std::size_t buffer_stride{ test_args.buffer_stride };
    const std::size_t queue_stride{ test_args.queue_stride };

    const std::size_t total_size{ test_args.total_size };
    const std::size_t buff_arr_size_total{ test_args.buff_arr_size };
    const std::size_t buff_arr_size_inflight{ test_args.buff_arr_size_inflight };
    const std::uint8_t queue_size{ static_cast<std::uint8_t>(test_args.queue_size) };
    const std::size_t queue_arr_size{ test_args.queue_arr_size };

    const std::size_t job_arr_size{ buffer_in_flight / 2 };

    LinearAllocator mem_allocator(total_size, test_args.cachel_size);

    using seg_struct = file_reverser::Segment;
    using item_task = file_reverser::Job;
    using spscq_item = SPSCLockFreeQ<std::uint8_t>;

    std::byte* buff_ptr = mem_allocator.allocate(buff_arr_size_total);
    std::byte* buff_queue = mem_allocator.allocate(queue_arr_size);
    std::uint8_t* read_work_buf = reinterpret_cast<std::uint8_t*>(buff_queue);
    std::uint8_t* work_write_buf = reinterpret_cast<std::uint8_t*>(buff_queue + queue_stride);
    std::uint8_t* write_read_buf = reinterpret_cast<std::uint8_t*>(buff_queue + (2 * queue_stride));

    spscq_item q_read_work_{ read_work_buf, queue_size };
    spscq_item q_work_write_{ work_write_buf, queue_size };
    spscq_item q_write_read_{ write_read_buf, queue_size };

    std::vector<item_task> job_arr;
    job_arr.reserve(job_arr_size);

    for (std::size_t i{ }, j{ }; i < buffer_in_flight; i += 2, ++j)
    {
        seg_struct carry = { &buff_ptr[i * buffer_stride], 0, 0 };
        seg_struct in = { &buff_ptr[(i + 1) * buffer_stride], 0, 0 };
        job_arr.push_back( item_task{ carry, in } );
        
        if (!q_write_read_.push(j))
        {
            throw std::invalid_argument("queue should not be getting full - pairs of item_to_write are less than the queue's capacity");
        }

    }

    
    using io_class = quantiq::io_raii;

    io_class io_input{ in_path.c_str() };
    io_input.ropen_internal();
    io_class io_output{ out_path.c_str() };
    io_output.wopen_internal();

    std::mutex read_work_mtx;
    std::condition_variable read_work_cv;

    std::mutex work_write_mtx;
    std::condition_variable work_write_cv;

    std::mutex write_read_mtx;
    std::condition_variable write_read_cv;


    auto read = [&](spscq_item& q_read_work, spscq_item& q_write_read)
    {
        std::optional<std::uint8_t> job_index{ };

        while (true)
        {
            job_index = q_write_read.pop();
            if (!job_index.has_value())
            {
                std::unique_lock<std::mutex> lck(write_read_mtx);
                write_read_cv.wait(lck, [&] { return !q_write_read.empty(); });
                job_index = q_write_read.pop();
            }
            
            auto& job_curr = job_arr[static_cast<std::size_t>(job_index.value())];
            auto& seg_in = job_curr.seg_[job_curr.seg_count_ - 1];
            seg_in.len_ = io_input.read(seg_in.buff_, buffer_size);

            if (q_read_work.push(job_index.value())) read_work_cv.notify_one();
            if (seg_in.len_ <= 0) break;
        }

    };

    auto work = [&](spscq_item& q_read_work, spscq_item& q_work_write, seg_struct seg_carry_prev)
    {
        std::optional<std::uint8_t> job_index{ };

        while (true)
        {
            job_index = q_read_work.pop();
            if (!job_index.has_value())
            {
                std::unique_lock<std::mutex> lck(read_work_mtx);
                read_work_cv.wait(lck, [&] { return !q_read_work.empty(); });
                job_index = q_read_work.pop();
            }
            
            auto& job_item = job_arr[static_cast<std::size_t>(job_index.value())];
            auto& seg_carry = job_item.seg_[0];
            auto& seg_in = job_item.seg_[1];

            file_reverser::utilities::mt::reverse_segment(seg_in, seg_carry, seg_carry_prev);

            if (q_work_write.push(job_index.value())) work_write_cv.notify_one();
            if (seg_in.len_ <= 0) break;
        }
    };

    auto write = [&](spscq_item& q_work_write, spscq_item& q_write_read)
    {
        std::optional<std::uint8_t> job_index{ };

        while (true)
        {
            job_index = q_work_write.pop();
            if (!job_index.has_value())
            {
                std::unique_lock<std::mutex> lck(work_write_mtx);
                work_write_cv.wait(lck, [&] { return !q_work_write.empty(); });
                job_index = q_work_write.pop();
            }

            auto& job_item = job_arr[static_cast<std::size_t>(job_index.value())];
            auto& seg_carry = job_item.seg_[0];
            auto& seg_in = job_item.seg_[1];

            if ( (seg_carry.len_ == 0) ^ (seg_in.len_ == 0))
            {
                auto *buf = (seg_carry.len_ > 0) ? seg_carry.buff_ : seg_in.buff_;
                std::size_t buf_len = (seg_carry.len_ > 0) ? seg_carry.len_ : seg_in.len_;
                io_output.write(buf, buf_len);
            }

            if (seg_carry.len_ > 0 && seg_in.len_ > 0)
            {
                iovec iov[2]{ };
                for (auto i = 0; i < job_item.seg_count_; ++i)
                {
                    iov[i].iov_base = static_cast<void*>(job_item.seg_[i].buff_ + job_item.seg_[i].off_);
                    iov[i].iov_len = job_item.seg_[i].len_;
                }

                io_output.writeall_v(iov, job_item.seg_count_);
            }

            if (seg_in.len_ <= 0) break;
            for (auto i = 0; i < job_item.seg_count_; ++i)
            {
                job_item.seg_[i].len_ = 0;
                job_item.seg_[i].off_ = 0;
            }

            if (q_write_read.push(job_index.value())) write_read_cv.notify_one();
        }
    };

    {
        seg_struct seg_carry_unique = { &buff_ptr[(buffer_count - 1) * buffer_stride], 0, 0 };
        std::jthread reader_thread{ read, std::ref(q_read_work_), std::ref(q_write_read_) };
        std::jthread worker_thread{ work, std::ref(q_read_work_), std::ref(q_work_write_), seg_carry_unique };
        std::jthread writer_thread{ write, std::ref(q_work_write_), std::ref(q_write_read_) };
    }


    io_input.close();
    io_output.close();

    return 0;
}