#include "../../include/file_reverser.hpp"
#include <print>

using namespace file_reverser;

int main(int argc, char* argv[])
{
    const std::size_t buffer_size = std::atoi(argv[1]);
    const std::size_t buffer_count = std::atoi(argv[2]);
    const std::size_t buffer_stride = round_up(buffer_size, cache_line_size);
    const std::size_t buff_arr_size = buffer_count * buffer_stride;
    const std::size_t queue_count = std::atoi(argv[3]);
    const std::size_t queue_cap = std::atoi(argv[4]);
    const std::size_t queue_size = sizeof(std::uint8_t) * queue_cap;
    const std::size_t queue_stride = round_up(queue_size, cache_line_size);
    const std::size_t queue_arr_size = queue_count * queue_stride;
    const std::size_t total_size = buff_arr_size + queue_arr_size;

    memory_mgr::LinearAllocator l_allocator{ total_size, cache_line_size };

    std::print("1. Total Size: {0} bytes\n", total_size);





}