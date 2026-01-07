#include "../../include/file_reverser.hpp"
#include "../../include/io_raii.hpp"
#include <string>
#include <memory>
#include <new>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <syncstream>
#include <vector>
#include <array>
#include <pthread.h>
#include <stdexcept>

#define sync_out std::osyncstream(std::cout)


constexpr std::size_t round_up(std::size_t n, std::size_t a) noexcept
{
    return (a == 0) ? n : ((n + (a - 1)) / a) * a;
}

constexpr std::size_t cacheline_stride() noexcept {
#if defined(__cpp_lib_hardware_interference_size)
    return std::hardware_destructive_interference_size;
#else
    return 64; // conservative fallback
#endif
}

struct AlignedArrayDeleter
{
    std::align_val_t al;
    void operator()(std::byte* p) const noexcept 
    {
        ::operator delete[](p, al);
    }
};



int main(int argc, char* argv[])
{
    std::string in_path(argv[1]);
    std::string out_path(argv[2]);


    const std::size_t buffer_size{ 8192 };
    const std::size_t buffer_count{ 9 };
    const std::size_t buffer_in_flight{ buffer_count - 1 };

    const std::size_t align = std::max<std::size_t>(cacheline_stride(), alignof(std::max_align_t));
    const std::size_t stride = round_up(buffer_size, align);
    const std::size_t total = buffer_count * stride;

    using buff_arr = std::unique_ptr<std::byte[], AlignedArrayDeleter>;

    std::byte* raw = static_cast<std::byte*>(::operator new[](total, std::align_val_t{ align }));
    buff_arr block(raw, AlignedArrayDeleter{ std::align_val_t{ align } });

    using seg_struct = file_reverser::Segment;
    using item_task = file_reverser::Job;
    using spscq_item = file_reverser::SPSC_LFQ<std::uint8_t>;

    const uint8_t queue_size = 16;


    spscq_item q_read_work_{ queue_size };
    spscq_item q_work_write_{ queue_size };
    spscq_item q_write_read_{ queue_size };


    // initialize the writer-reader queue with empty segments


    assert(q_write_read_.empty());

    std::vector<item_task> job_arr;

    // for (std::size_t i{ }; i < buffer_in_flight; i += 2)
    // {

    // }

    
    for (std::size_t i{ }, j{ }; i < buffer_in_flight; i += 2, ++j)
    {
        seg_struct carry = { &raw[i * stride], 0, 0 };
        seg_struct in = { &raw[(i + 1) * stride], 0, 0 };
        job_arr.push_back(item_task{ carry, in });

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

    // auto read = [&](spscq_item& spscq, )
    auto read = [&](spscq_item& q_read_work, spscq_item& q_write_read)
    {
        std::uint8_t job_index{ };

        while (true)
        {
            /** @performance: accessing acquire - acquire 
             *  synchronization required each time empty() called */
            if (q_write_read.empty())
            {
                std::unique_lock<std::mutex> lck(write_read_mtx);
                write_read_cv.wait(lck, [&] { return !q_write_read.empty(); });
            }

            if (!q_write_read.pop(job_index)) throw std::runtime_error("Read Thread: Pop Returned False\n");
            auto& job_curr = job_arr[static_cast<std::size_t>(job_index)];
            auto& seg_in = job_curr.seg_[job_curr.seg_count_ - 1];

            assert(seg_in.len_ == 0);

            seg_in.len_ = io_input.read(seg_in.buff_, buffer_size);

            // if (segment_in.len_ > 0)
            // {
            //     std::string reversed(reinterpret_cast<char*>(segment_in.buff_), segment_in.len_);
            //     sync_out << "\nRead String\n" << reversed << "\n\n";
            // }

            // sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Reader\n";
            // sync_out << "Item->Segment[0]: " << read_item.seg_[0].buff_ << " -> len_: " << read_item.seg_[0].len_ << ", off_: " << read_item.seg_[0].off_ << "\n";
            // sync_out << "Item->Segment[1]: " << read_item.seg_[1].buff_ << " -> len_: " << read_item.seg_[1].len_ << ", off_: " << read_item.seg_[1].off_ << "\n";


            bool was_empty = q_read_work.empty();
            
            if (q_read_work.push(job_index))
            {
                if (was_empty)
                {
                    read_work_cv.notify_one();
                }
            }

            if (seg_in.len_ <= 0) break;
        }
    };


    auto work = [&](spscq_item& q_read_work, spscq_item& q_work_write, seg_struct seg_carry_prev)
    {
        std::uint8_t job_index{ };

        while (true)
        {
            if (q_read_work.empty())
            {
                std::unique_lock<std::mutex> lck(read_work_mtx);
                read_work_cv.wait(lck, [&] { return !q_read_work.empty(); });
            }

            if (!q_read_work.pop(job_index)) throw std::runtime_error("Worker Thread: Pop Returned False\n");

            // sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Worker -> Before Processing\n";
            // sync_out << "Item->Segment[0]: " << work_item.seg_[0].buff_ << " -> len_: " << work_item.seg_[0].len_ << ", off_: " << work_item.seg_[0].off_ << "\n";
            // sync_out << "Item->Segment[1]: " << work_item.seg_[1].buff_ << " -> len_: " << work_item.seg_[1].len_ << ", off_: " << work_item.seg_[1].off_ << "\n";

            auto& job_item = job_arr[static_cast<std::size_t>(job_index)];
            auto& seg_carry = job_item.seg_[0];
            auto& seg_in = job_item.seg_[1];

            file_reverser::utilities::mt::reverse_segment(seg_in, seg_carry, seg_carry_prev);
            // if (seg_in.len_ == 0 && seg_carry.len_ > 0)
            // {
            //     std::string reversed(reinterpret_cast<char*>(seg_carry.buff_), seg_carry.len_);
            //     sync_out << "\nReversed String\n" << reversed << "\n\n";
            // }

            bool was_empty = q_work_write.empty();

            if (q_work_write.push(job_index))
            {
                if (was_empty)
                {
                    work_write_cv.notify_one();
                }
            }

            if (seg_in.len_ <= 0) break;

            // if (item.seg_[0].len_ > 0)
            // {
            //     std::string reversed(reinterpret_cast<char*>(item.seg_[0].buff_), item.seg_[0].len_);
            //     sync_out << "\n\nReversed String\n" << reversed << "\n\n";
            // }



            // if (!item.seg_[1].buff_)
            // {
            //     item.seg_[1] = work_item.seg_[0];
            //     ++(item.seg_count_);
            // }

            // sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Worker -> After Processing\n";
            // sync_out << "Item->Segment[0]: " << item.seg_[0].buff_ << " -> len_: " << item.seg_[0].len_ << ", off_: " << item.seg_[0].off_ << "\n";
            // sync_out << "Item->Segment[1]: " << item.seg_[1].buff_ << " -> len_: " << item.seg_[1].len_ << ", off_: " << item.seg_[1].off_ << "\n";

        }

    };


    auto write = [&](spscq_item& q_work_write, spscq_item& q_write_read)
    {
        std::uint8_t job_index{ };

        while (true)
        {
            if (q_work_write.empty())
            {
                std::unique_lock<std::mutex> lck(work_write_mtx);
                work_write_cv.wait(lck, [&] { return !q_work_write.empty(); });
            }

            if (!q_work_write.pop(job_index)) throw std::runtime_error("Writer Thread: Pop Returned False\n");
            
            // sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Writer -> Before Write\n";
            // sync_out << "Item->Segment[0]: " << write_item.seg_[0].buff_ << " -> len_: " << write_item.seg_[0].len_ << ", off_: " << write_item.seg_[0].off_ << "\n";
            // sync_out << "Item->Segment[1]: " << write_item.seg_[1].buff_ << " -> len_: " << write_item.seg_[1].len_ << ", off_: " << write_item.seg_[1].off_ << "\n";

            auto& job_item = job_arr[static_cast<std::size_t>(job_index)];
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
                // std::memset(job_item.seg_[i].buff_, 0, buffer_size);
            }
            
            // std::memset(write_item.seg_[0].buff_, 0, buffer_size);
            // std::memset(write_item.seg_[1].buff_, 0, buffer_size);

            bool was_empty = q_write_read.empty();

            if (q_write_read.push(job_index))
            {
                if (was_empty)
                {
                    write_read_cv.notify_one();
                }
            }

        }
    };


    std::array<std::jthread, 3> threads;

    {
        seg_struct seg_carry_unique = { &raw[(buffer_count - 1) * stride], 0, 0 };

        for (std::size_t i{ }; i < threads.size(); ++i)
        {
            if (i == 0) threads[i] = std::jthread{ read, std::ref(q_read_work_), std::ref(q_write_read_) };
            else if (i == 1) threads[i] = std::jthread{ work, std::ref(q_read_work_), std::ref(q_work_write_), seg_carry_unique };
            else /* i == 2 */ threads[i] = std::jthread{ write, std::ref(q_work_write_), std::ref(q_write_read_) };

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(static_cast<int>(i), &cpuset);
            int rc = pthread_setaffinity_np(threads[i + i].native_handle(), sizeof(cpuset), &cpuset);
            if (rc != 0)
            {
                throw std::runtime_error("Error calling pthread_setaffinity_np\n");
            }
        }
    }

    // ensure threads are finished before closing fds
    for (auto& t : threads)
    {
        if (t.joinable()) t.join();
    }

    io_input.close();
    io_output.close();

    return 0;

}