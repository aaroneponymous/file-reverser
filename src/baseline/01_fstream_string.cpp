#include <fstream>
#include <string>
#include <algorithm>

int main()
{
    std::string in_path{ "./input/crime_and_punishment.txt" };
    std::string out_path{ "./output/crime_and_punishment.txt" };

    std::ifstream in(in_path, std::ios::binary);
    std::ofstream out(out_path, std::ios::binary);
    out << in.rdbuf();


    return 0;
}