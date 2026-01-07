#include <fstream>
#include <string>
#include <algorithm>
#include <iostream>

int main()
{
    const std::string in_path  = "../input/crime_and_punishment.txt";
    const std::string out_path = "../output/crime_and_punishment_fstream.txt";

    std::ifstream in(in_path, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open input: " << in_path << "\n";
        return 1;
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output: " << out_path << "\n";
        return 1;
    }

    // Optional: reduce syscalls a bit (may or may not be honored by the lib).
    // static char in_buf[1 << 20];
    // static char out_buf[1 << 20];
    // in.rdbuf()->pubsetbuf(in_buf, sizeof(in_buf));
    // out.rdbuf()->pubsetbuf(out_buf, sizeof(out_buf));

    std::string line;
    line.reserve(4096);

    while (true) {
        line.clear();

        bool saw_any = false;
        bool had_lf  = false;

        char ch;
        while (in.get(ch)) {
            saw_any = true;
            if (ch == '\n') {          // end of line (LF)
                had_lf = true;
                break;
            }
            line.push_back(ch);        // keep everything else (including possible '\r')
        }

        if (!saw_any) break;           // no more bytes to read

        // Preserve Windows CRLF exactly: if we saw '\n' and the line ends in '\r', treat that as EOL.
        const bool had_crlf = had_lf && !line.empty() && line.back() == '\r';
        if (had_crlf) line.pop_back();

        std::reverse(line.begin(), line.end());
        out.write(line.data(), static_cast<std::streamsize>(line.size()));

        if (had_lf) {
            if (had_crlf) out.put('\r');
            out.put('\n');
        }
        // else: last line had no trailing '\n' -> don't add one
    }

    if (!out) {
        std::cerr << "Write failed.\n";
        return 1;
    }

    return 0;
}
