/** 
* As we've seen earlier, command-line tools like taskset let us control the CPU affinity 
* of a whole process. Sometimes, however, we'd like to do something more fine-grained 
* and set the affinities of specific threads from within the program. 
* How do we do that?
*/


/**
*
* On Linux, we can use the pthread-specific pthread_setaffinity_np function. 
* Here's an example that reproduces what we did before, but this time from inside the program. 
* In fact, let's go a bit more fancy and pin each thread to a single known CPU by 
* setting its affinity:
*/


#include <iostream>
#include <thread>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <chrono>

int main(int argc, const char** argv) {
  constexpr unsigned num_threads = 4;
  // A mutex ensures orderly access to std::cout from multiple threads.
  std::mutex iomutex;
  std::vector<std::thread> threads(num_threads);
  for (unsigned i = 0; i < num_threads; ++i) {
    threads[i] = std::thread([&iomutex, i] {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      while (1) {
        {
          // Use a lexical scope and lock_guard to safely lock the mutex only
          // for the duration of std::cout usage.
          std::lock_guard<std::mutex> iolock(iomutex);
          std::cout << "Thread #" << i << ": on CPU " << sched_getcpu() << "\n";
        }

        // Simulate important work done by the tread by sleeping for a bit...
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
      }
    });

    // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
    // only CPU i as set.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(i, &cpuset);
    int rc = pthread_setaffinity_np(threads[i].native_handle(),
                                    sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
      std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
  }

  for (auto& t : threads) {
    t.join();
  }
  return 0;
}
