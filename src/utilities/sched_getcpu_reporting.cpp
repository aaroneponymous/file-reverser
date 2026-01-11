#include <iostream>
#include <syncstream>
#include <thread>
#include <vector>
#include <sched.h>
#include <chrono>

#define sync_out std::osyncstream(std::cout)


int main()
{
	constexpr unsigned num_threads = 8;
    std::vector<std::jthread> threads(num_threads);

    for (unsigned i{ }; i < num_threads; ++i)
    {
        threads[i] = std::jthread([i]{
            while (true)
            {
                sync_out << "Thread #" << i << ": on CPU " << sched_getcpu() << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
                
            }
        });
    }

    return 0;

}
