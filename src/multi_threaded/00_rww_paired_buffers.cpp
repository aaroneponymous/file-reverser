#include "../../include/file_reverser.h"
#include "../../include/io_raii.h"
#include <string>
#include <memory>
#include <new>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <syncstream>
#include <string>

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



int main()
{
    std::string in_path{ "../input/hamlet.txt" };
    std::string out_path{ "../output/hamlet.txt" };


    const std::size_t buffer_size{ 4096 };
    const std::size_t buffer_count{ 9 };
    const std::size_t buffer_in_flight{ buffer_count - 1 };

    const std::size_t align = std::max<std::size_t>(cacheline_stride(), alignof(std::max_align_t));
    const std::size_t stride = round_up(buffer_size, align);
    const std::size_t total = buffer_count * stride;

    using buff_arr = std::unique_ptr<std::byte[], AlignedArrayDeleter>;

    std::byte* raw = static_cast<std::byte*>(::operator new[](total, std::align_val_t{ align }));
    buff_arr block(raw, AlignedArrayDeleter{ std::align_val_t{ align } });

    using seg_struct = file_reverser::Segment;
    using item_task = file_reverser::WriteItem;
    using spscq_item = file_reverser::SPSC_LFQ<item_task>;

    const uint8_t queue_size = 16;

    // auto buf_read_work = std::make_unique<seg_struct[]>(queue_size);
    // auto buf_work_write = std::make_unique<seg_struct[]>(queue_size);
    // auto buf_write_read = std::make_unique<seg_struct[]>(queue_size);


    // auto buf_read_work = new seg_struct[queue_size];
    // auto buf_work_write = new seg_struct[queue_size]; 
    // auto buf_write_read = new seg_struct[queue_size]; 



    spscq_item q_read_work_{ queue_size };
    spscq_item q_work_write_{ queue_size };
    spscq_item q_write_read_{ queue_size };


    // initialize the writer-reader queue with empty segments


    assert(q_write_read_.empty());

    
    for (std::size_t i{ }; i < buffer_in_flight; i += 2)
    {
        seg_struct in = { &raw[i], 0, 0 };
        seg_struct carry = { &raw[i + 1], 0, 0 };

        if (!q_write_read_.emplace_push(item_task{ in, carry }))
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
        item_task read_item;

        while (true)
        {
            if (q_write_read.empty())
            {
                std::unique_lock<std::mutex> lck(write_read_mtx);
                write_read_cv.wait(lck, [&] { return !q_write_read.empty(); });
            }

            // std::memset(read_item.seg_[0].buff_, 0, buffer_size);
            // std::memset(read_item.seg_[1].buff_, 0, buffer_size);

            q_write_read.pop(read_item);

            auto& segment_in = read_item.seg_[1];
            segment_in.len_ = io_input.read(segment_in.buff_, buffer_size);

            if (segment_in.len_ > 0)
            {
                std::string reversed(reinterpret_cast<char*>(segment_in.buff_), segment_in.len_);
                sync_out << "\nRead String\n" << reversed << "\n\n";
            }

            sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Reader\n";
            sync_out << "Item->Segment[0]: " << read_item.seg_[0].buff_ << " -> len_: " << read_item.seg_[0].len_ << ", off_: " << read_item.seg_[0].off_ << "\n";
            sync_out << "Item->Segment[1]: " << read_item.seg_[1].buff_ << " -> len_: " << read_item.seg_[1].len_ << ", off_: " << read_item.seg_[1].off_ << "\n";



            if (segment_in.len_ < 0) break;

            bool was_empty = q_read_work.empty();
            
            if (q_read_work.push(read_item))
            {
                if (was_empty)
                {
                    read_work_cv.notify_one();
                }
            }

            if (segment_in.len_ <= 0) break;
        }
    };


    auto work = [&](spscq_item& q_read_work, spscq_item& q_work_write, seg_struct carry_unique)
    {
        item_task work_item;

        while (true)
        {
            if (q_read_work.empty())
            {
                std::unique_lock<std::mutex> lck(read_work_mtx);
                read_work_cv.wait(lck, [&] { return !q_read_work.empty(); });
            }

            q_read_work.pop(work_item);

            sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Worker -> Before Processing\n";
            sync_out << "Item->Segment[0]: " << work_item.seg_[0].buff_ << " -> len_: " << work_item.seg_[0].len_ << ", off_: " << work_item.seg_[0].off_ << "\n";
            sync_out << "Item->Segment[1]: " << work_item.seg_[1].buff_ << " -> len_: " << work_item.seg_[1].len_ << ", off_: " << work_item.seg_[1].off_ << "\n";

            auto item = file_reverser::utilities::st::reverse_segment(work_item.seg_[1], work_item.seg_[0], carry_unique);

            if (item.seg_[0].len_ > 0)
            {
                std::string reversed(reinterpret_cast<char*>(item.seg_[0].buff_), item.seg_[0].len_);
                sync_out << "\n\nReversed String\n" << reversed << "\n\n";
            }


            bool was_empty = q_work_write.empty();


            if (!item.seg_[1].buff_)
            {
                item.seg_[1] = work_item.seg_[0];
                ++(item.seg_count_);
            }

            sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Worker -> After Processing\n";
            sync_out << "Item->Segment[0]: " << item.seg_[0].buff_ << " -> len_: " << item.seg_[0].len_ << ", off_: " << item.seg_[0].off_ << "\n";
            sync_out << "Item->Segment[1]: " << item.seg_[1].buff_ << " -> len_: " << item.seg_[1].len_ << ", off_: " << item.seg_[1].off_ << "\n";



            if (q_work_write.push(item))
            {
                if (was_empty)
                {
                    work_write_cv.notify_one();
                }
            }

            if (work_item.seg_[1].len_ <= 0) break;

        }

    };


    auto write = [&](spscq_item& q_work_write, spscq_item& q_write_read)
    {
        item_task write_item;

        while (true)
        {
            if (q_work_write.empty())
            {
                std::unique_lock<std::mutex> lck(work_write_mtx);
                work_write_cv.wait(lck, [&] { return !q_work_write.empty(); });
            }

            q_work_write.pop(write_item);
            
            sync_out << "\nThread[" << std::this_thread::get_id() << "]: " << "Writer -> Before Write\n";
            sync_out << "Item->Segment[0]: " << write_item.seg_[0].buff_ << " -> len_: " << write_item.seg_[0].len_ << ", off_: " << write_item.seg_[0].off_ << "\n";
            sync_out << "Item->Segment[1]: " << write_item.seg_[1].buff_ << " -> len_: " << write_item.seg_[1].len_ << ", off_: " << write_item.seg_[1].off_ << "\n";

            if (write_item.seg_count_ > 0)
            {
                iovec iov[2]{ };
                for (auto i = 0; i < write_item.seg_count_; ++i)
                {
                    iov[i].iov_base = static_cast<void*>(write_item.seg_[i].buff_ + write_item.seg_[i].off_);
                    iov[i].iov_len = write_item.seg_[i].len_;
                }

                io_output.writeall_v(iov, write_item.seg_count_);
            }

            if (write_item.seg_[1].len_ <= 0) break;

            for (auto i = 0; i < write_item.seg_count_; ++i)
            {
                write_item.seg_[i].len_ = 0;
                write_item.seg_[i].off_ = 0;
            }
            
            std::memset(write_item.seg_[0].buff_, 0, buffer_size);
            std::memset(write_item.seg_[1].buff_, 0, buffer_size);

            bool was_empty = q_write_read.empty();

            if (q_write_read.push(write_item))
            {
                if (was_empty)
                {
                    write_read_cv.notify_one();
                }
            }

        }
    };


    {
        seg_struct seg_carry_unique = { &raw[buffer_count - 1], 0, 0 };

        std::jthread reader_thread{ read, std::ref(q_read_work_), std::ref(q_write_read_) };
        std::jthread worker_thread{ work, std::ref(q_read_work_), std::ref(q_work_write_), seg_carry_unique };
        std::jthread writer_thread{ write, std::ref(q_work_write_), std::ref(q_write_read_) };
    }


    io_input.close();
    io_output.close();

    return 0;
}