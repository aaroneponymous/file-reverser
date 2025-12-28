#include <string>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sys/uio.h>
#include "../io_raii.h"
#include "../file_reverser.h"

// #define BUFFSIZE 4096  // 4 KiB 
#define BUFFSIZE 8192  // 8 KiB
// #define BUFFSIZE 131072   // 1 MiB
// #define BUFFSIZE 262144   // 2 MiB
// #define BUFFSIZE 524'288     // 5 MiB



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


  while (true) 
  {
      auto n = input.read(seg_in.buff_, BUFFSIZE);
      seg_in.len_ = static_cast<std::size_t>(n);
      seg_in.off_ = 0;

      if (n > 0) 
      {
          auto item = file_reverser::utilities::st::reverse_segment(seg_in, carry_seg_a, carry_seg_b);

          iovec iov[2]{};
          for (int i = 0; i < item.seg_count_; ++i) 
          {
              iov[i].iov_base = item.seg_[i].buff_ + item.seg_[i].off_;
              iov[i].iov_len  = item.seg_[i].len_;
          }
          output.writeall_v(iov, item.seg_count_);
      }

      if (n <= 0) break;
  }


  input.close();
  output.close();

} 