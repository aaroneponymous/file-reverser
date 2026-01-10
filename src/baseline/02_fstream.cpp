#include <fstream>
#include <string>
#include <cstddef>
#include <vector>

#include "../../include/file_reverser.hpp"


struct Args
{
    std::string in;
    std::string out;
};

int main(int argc, char* argv[])
{
    const Args args = { std::string{ argv[1] }, std::string{ argv[2] } };

    std::ifstream in{ args.in, std::ios::binary };
    if (!in) throw std::runtime_error("cannot open input file");

    std::ofstream out{ args.out, std::ios::binary };
    if (!out) throw std::runtime_error("cannot open output file");

    std::string line;
    line.reserve(LINE_SIZE);

    /** keep '\n' in string: @reference: https://www.reddit.com/r/cpp_questions/comments/9uphye/is_there_a_way_to_have_stdgetline_keep_new_line/ */

    while (std::getline(in, line))
    {
        // if (!in.eof()) { line.push_back('\n'); }
        auto line_size = line.size();
        const auto has_carriage = !line.empty() && line.back() == '\r';
        if (has_carriage) {
            line.pop_back();
        }

        std::span<std::byte> line_span{ reinterpret_cast<std::byte*>(line.data()),
            line.size() };
        file_reverser::utilities::reverse_range(line_span, 0, line_span.size());
        if (has_carriage) {
            line.push_back('\r');
        }
        line.push_back('\n');

        out.write(line.data(), static_cast<std::streamsize>(line.size()));
    }


    in.close();
    out.close();



    // const auto& input_path = args.in;
    // const auto& output_path = args.out;
    // const auto buffer_size = args.buf_size; 


    // // buffers via std::string
    // std::array<std::string, 3> buff_arr;
    // for (std::size_t i{ }; i < 3; ++i)
    // {
    //     std::string buff;
    //     buff.reserve(buffer_size);
    //     buff_arr[i] = std::move(buff);
    // }

    // // segments
    // file_reverser::Segment seg_in{ reinterpret_cast<std::byte*>(buff_arr[0].data()), 0, 0 };
    // file_reverser::Segment seg_carry{ reinterpret_cast<std::byte*>(buff_arr[1].data()), 0, 0 };
    // file_reverser::Segment seg_carry_prev{ reinterpret_cast<std::byte*>(buff_arr[2].data()), 0, 0 };

}
