#include "../io_raii.h"
#include <string>
#include <algorithm>
#include <filesystem>
#include "../in_place_reverser.h"

#define BUFFSIZE 4096  // 4 KiB 
// #define BUFFSIZE 8192  // 8 KiB
// #define BUFFSIZE 131072   // 1 MiB
// #define BUFFSIZE 262144   // 2 MiB
// #define BUFFSIZE 524'288     // 5 MiB

int main()
{
  std::string in_path("../../input/00_one_line.txt");
  std::string out_path("../../output/00_one_line.txt");

  quantiq::io_raii input;
  quantiq::io_raii output;

  std::byte BUFF[BUFFSIZE];

  input.ropen(in_path.data());
  output.wopen(out_path.data());

  while (!input.is_eof())
  {
    auto n = input.read(static_cast<void*>(BUFF), BUFFSIZE);
    if (n > 0) { 
      std::span<std::byte> buf_span{BUFF, static_cast<std::size_t>(n)};
      auto is_reversed = utf8_inplace::reverse_inpleace(buf_span, false);
      output.write(static_cast<void*>(BUFF), n); 
    }
  }

  input.close();
  output.close();

} 