// #include "../io_raii.h"
// #include <string>
// #include <algorithm>
// #include <filesystem>
// #include "../in_place_reverser.h"
// #include <iostream>

// #define BUFFSIZE 4096  // 4 KiB 
// // #define BUFFSIZE 8192  // 8 KiB
// // #define BUFFSIZE 131072   // 1 MiB
// // #define BUFFSIZE 262144   // 2 MiB
// // #define BUFFSIZE 524'288     // 5 MiB



// /**
//  * @todo: I'm skipping tabs
//  * - See 01_more_lines.txt
//  */

// int main()
// {
//   std::string in_path("../../input/01_more_lines.txt");
//   std::string out_path("../../output/01_more_lines.txt");

//   quantiq::io_raii input;
//   quantiq::io_raii output;

//   std::byte BUFF[BUFFSIZE];
//   std::byte CARRY[BUFFSIZE];


//   utf8_inplace::buffer_descriptor buffer = { BUFF, 0, 0 };
//   utf8_inplace::buffer_descriptor carry = { CARRY, 0, 0 };





//   input.ropen(in_path.data());
//   output.wopen(out_path.data());

//   std::size_t bytes_carried{ };

//   while (true)
//   {
//     auto n = input.read(static_cast<void*>(buffer.buf_), BUFFSIZE);
//     buffer.valid_bytes_ = n;
  
//     if (n >= 0)
//     {
//       auto reversed = utf8_inplace::reverse_buffers(buffer, BUFFSIZE, carry, BUFFSIZE);
//       if (reversed)
//       {
//         std::cout << "bytes reversed: " << buffer.reversed_bytes << "\n\n";
//         output.write(static_cast<void*>(buffer.buf_), buffer.reversed_bytes);
//         buffer.reversed_bytes = 0;
//         buffer.valid_bytes_ = 0;
//       }
//     }

//     if (n == 0) break;
//   }

//   input.close();
//   output.close();

// } 