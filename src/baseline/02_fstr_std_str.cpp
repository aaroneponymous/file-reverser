#include <fstream>
#include <string>
#include <cstddef>
#include <iostream>
#include "../../include/file_reverser.hpp"

struct Args
{
    std::string in;
    std::string out;
    std::size_t buf_size{ };
};

int main(int argc, char argv[])
{
    const Args args = { std::string{ argv[1] }, std::string{ argv[2] }, std::stoul(std::string{ argv[3] }) };
    std::array<std::string, 3> string_arr;

    for (auto& str : string_arr) {
        str.reserve(args.buf_size);
    }

    file_reverser::Segment seg_in{ reinterpret_cast<std::byte*>(string_arr[0].data()), 0, 0 };
    file_reverser::Segment seg_carry{ reinterpret_cast<std::byte*>(string_arr[1].data()), 0, 0 };
    file_reverser::Segment seg_carry_prev{ reinterpret_cast<std::byte*>(string_arr[2].data()), 0, 0 };




// create three strings and use the underlying as buffer
}
