#include "../io_raii.h"
#include <string>
#include <algorithm>
#include <filesystem>
#include "../in_place_reverser.h"
#include <iostream>

#define BUFFSIZE 4096  // 4 KiB 
// #define BUFFSIZE 8192  // 8 KiB
// #define BUFFSIZE 131072   // 1 MiB
// #define BUFFSIZE 262144   // 2 MiB
// #define BUFFSIZE 524'288     // 5 MiB

struct buffer_descriptor
{
  std::byte* buffer;
};


/**
 * @todo: I'm skipping tabs
 * - See 01_more_lines.txt
 */

int main()
{
  std::string in_path("../../input/hamlet.txt");
  std::string out_path("../../output/hamlet.txt");

  quantiq::io_raii input;
  quantiq::io_raii output;

  std::byte BUFF[BUFFSIZE];
  std::byte CARRY[BUFFSIZE];


  buffer_descriptor buffer;
  buffer.buffer = BUFF;
  buffer_descriptor carry;
  carry.buffer = CARRY;


  input.ropen(in_path.data());
  output.wopen(out_path.data());

  std::size_t bytes_carried{ };

  while (true)
  {
    auto n = input.read(static_cast<void*>(buffer.buffer), BUFFSIZE);
    if (n >= 0)
    { 
      auto bytes_reversed = utf8_inplace::reverse_inplace(buffer.buffer, n, BUFFSIZE, carry.buffer, BUFFSIZE, bytes_carried);
      if (bytes_reversed > 0) 
      {
        std::cout << "bytes reversed: " << bytes_reversed << "\n\n";
        output.write(static_cast<void*>(buffer.buffer), bytes_reversed); 
      }
    }

    if (n == 0) { break; }
  }

  input.close();
  output.close();

} 