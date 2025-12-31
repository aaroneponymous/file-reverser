#include <string>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <new>        // std::hardware_destructive_interference_size
#include <sys/uio.h>

#include "../../include/io_raii.h"
#include "../../include/file_reverser.h"

namespace {

struct Args {
    std::string in_path;
    std::string out_path;
    std::size_t buf_size = 4096;
};

[[noreturn]] void usage(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " --in <input> --out <output> [--buf <bytes>]\n\n"
        << "Options:\n"
        << "  --in   Input file path\n"
        << "  --out  Output file path\n"
        << "  --buf  Buffer size in bytes (default 4096)\n";
    std::exit(2);
}

std::size_t parse_size(const char* s) {
    errno = 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        throw std::runtime_error(std::string("Invalid size: ") + s);
    }
    if (v == 0) {
        throw std::runtime_error("Buffer size must be > 0");
    }
    return static_cast<std::size_t>(v);
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto need_value = [&]() -> const char* {
            if (i + 1 >= argc) usage(argv[0]);
            return argv[++i];
        };

        if (std::strcmp(arg, "--in") == 0) {
            a.in_path = need_value();
        } else if (std::strcmp(arg, "--out") == 0) {
            a.out_path = need_value();
        } else if (std::strcmp(arg, "--buf") == 0) {
            a.buf_size = parse_size(need_value());
        } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            usage(argv[0]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            usage(argv[0]);
        }
    }

    if (a.in_path.empty() || a.out_path.empty()) usage(argv[0]);

    // Your current design assumes carry buffers are the same size as the read buffer.
    // With your invariant "max line total <= 4096", require buf >= 4096 so carry always fits.
    if (a.buf_size < 4096) {
        throw std::runtime_error("--buf must be >= 4096 given MAX_LINE_TOTAL=4096 and carry buffers sized to --buf");
    }

    return a;
}

constexpr std::size_t round_up(std::size_t n, std::size_t a) noexcept {
    return (a == 0) ? n : ((n + (a - 1)) / a) * a;
}

constexpr std::size_t cacheline_stride() noexcept {
#if defined(__cpp_lib_hardware_interference_size)
    return std::hardware_destructive_interference_size;
#else
    return 64; // conservative fallback
#endif
}

struct AlignedArrayDeleter {
    std::align_val_t al;
    void operator()(std::byte* p) const noexcept {
        ::operator delete[](p, al);
    }
};

} // namespace

int main(int argc, char** argv) try
{
    const Args args = parse_args(argc, argv);

    // One allocation, carved into 3 buffers.
    // We round each buffer up to a cache-line stride so adjacent buffers don’t share a cache line at the boundary.
    const std::size_t align  = std::max<std::size_t>(cacheline_stride(), alignof(std::max_align_t));
    const std::size_t stride = round_up(args.buf_size, align);
    const std::size_t total  = 3 * stride;

    std::byte* raw = static_cast<std::byte*>(::operator new[](total, std::align_val_t{align}));
    std::unique_ptr<std::byte[], AlignedArrayDeleter> block(raw, AlignedArrayDeleter{std::align_val_t{align}});

    std::byte* buf_in  = raw + 0 * stride;
    std::byte* carry_a = raw + 1 * stride;
    std::byte* carry_b = raw + 2 * stride;

    quantiq::io_raii input;
    quantiq::io_raii output;

    file_reverser::Segment seg_in{ buf_in, 0, 0 };
    file_reverser::Segment carry_seg_a{ carry_a, 0, 0 };
    file_reverser::Segment carry_seg_b{ carry_b, 0, 0 };

    input.ropen(args.in_path.c_str());
    output.wopen(args.out_path.c_str());

    while (true)
    {
        const auto n = input.read(seg_in.buff_, args.buf_size);
        seg_in.len_ = static_cast<std::size_t>(n);
        seg_in.off_ = 0;

        if (n >= 0)
        {
            auto item = file_reverser::utilities::st::reverse_segment(seg_in, carry_seg_a, carry_seg_b);

            if (item.seg_count_ > 0) {
                iovec iov[2]{};
                for (int i = 0; i < item.seg_count_; ++i)
                {
                    iov[i].iov_base = static_cast<void*>(item.seg_[i].buff_ + item.seg_[i].off_);
                    iov[i].iov_len  = item.seg_[i].len_;
                }
                output.writeall_v(iov, item.seg_count_);
            }
        }

        if (n <= 0) break;
    }

    input.close();
    output.close();
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << "error: " << e.what() << "\n";
    return 1;
}
