#include <iostream>
#include <thread>


int main()
{
	auto num_cpus = std::thread::hardware_concurrency();
	std::cout << "Number of CPUs: " << num_cpus << "\n";

	return 0;
}
