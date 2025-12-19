#include <fstream>
#include <string>
#include <algorithm>

int main()
{
    std::string in_path{ "./input/crime_and_punishment.txt" };
    std::string out_path{ "./output/crime_and_punishment.txt" };

    std::ifstream in{ in_path };
    std::ofstream out{ out_path };

    std::string line;

    while(std::getline(in, line))
    {
        std::reverse(line.begin(), line.end());
        out << line << "\n";
    }

    return 0

}