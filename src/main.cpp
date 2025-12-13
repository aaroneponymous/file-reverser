#include <algorithm>
#include <codecvt>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>

static std::u32string utf8_to_u32(const std::string& s) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cvt;
    return cvt.from_bytes(s);
}

static std::string u32_to_utf8(const std::u32string& s) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cvt;
    return cvt.to_bytes(s);
}

int main() {
    std::ifstream in("../input/00_lines_23.txt");
    std::ofstream out("../output/00_output.txt"); // truncate

    std::string line;
    while (std::getline(in, line)) {
        auto u32 = utf8_to_u32(line);
        std::reverse(u32.begin(), u32.end());
        out << u32_to_utf8(u32) << '\n';
    }
}


// #include <iostream>
// #include <string>
// #include <iterator> // Required for std::iter_swap

// int main() {
//     std::string str = "C++ Iterators";
    
//     // Manual in-place reversal with iterators
//     std::string::iterator start = str.begin();
//     std::string::iterator end = str.end() - 1; // Point to the last character
    
//     while (std::distance(start, end) > 0) { // While the distance is positive
//         std::iter_swap(start, end); // Swap characters
//         ++start; // Move start forward
//         --end;   // Move end backward
//     }
    
//     std::cout << str << std::endl; // Output: "srotarætI ++C"
    
//     return 0;
// }



