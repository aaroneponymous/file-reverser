#include <string>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sys/uio.h>
#include "../io_raii.h"
#include "../file_reverser.h"

#define BUFFSIZE 4096  // 4 KiB 
// #define BUFFSIZE 8192  // 8 KiB
// #define BUFFSIZE 131072   // 1 MiB
// #define BUFFSIZE 262144   // 2 MiB
// #define BUFFSIZE 524'288     // 5 MiB



/**
 * @todo: I'm skipping tabs
 * - See 01_more_lines.txt
 */

int main()
{
  std::string in_path("../../input/crime_and_punishment.txt");
  std::string out_path("../../output/crime_and_punishment.txt");

  quantiq::io_raii input;
  quantiq::io_raii output;

  std::byte buffer_default[BUFFSIZE];
  std::byte carry_a[BUFFSIZE];
  std::byte carry_b[BUFFSIZE];

  file_reverser::Segment seg_in{ buffer_default, 0, 0 };
  file_reverser::Segment carry_seg_a{ carry_a, 0, 0 };
  file_reverser::Segment carry_seg_b{ carry_b, 0, 0 };


  input.ropen(in_path.data());
  output.wopen(out_path.data());

  std::size_t bytes_carried{ };

  while (true)
  {
    auto n = input.read(seg_in.buff_, BUFFSIZE);
    seg_in.off_ = 0;
    seg_in.len_ = n;
  
    if (n > 0)
    {
        file_reverser::WriteItem item = file_reverser::reverse_segment(seg_in, carry_seg_a, carry_seg_b);
        iovec iov[2] = { { item.seg_[0].buff_ + item.seg_[0].off_, item.seg_[0].len_ },
                         { item.seg_[1].buff_ + item.seg_[1].off_, item.seg_[1].len_ }};

        output.writeall_v(iov, 2);
        item.seg_[0].len_ = item.seg_[0].off_ = 0;
        item.seg_[1].len_ = item.seg_[1].off_ = 0;
    }

    if (n == 0) {
        if (carry_seg_a.len_ > 0)
        {
            std::span<std::byte> eof{ carry_seg_a.buff_, carry_seg_a.len_ };
            file_reverser::reverse_range(eof, 0, carry_seg_a.len_);
            output.write(eof.data(), eof.size());
        }

        break;
    }
  }

  input.close();
  output.close();

} 