#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <cstddef>


namespace quantiq
{
    class io_raii
    {
        public:
            io_raii() = default;

            ~io_raii() 
            {
                close();
            }

            void ropen(const char* path)
            {
                if (is_set()) throw std::logic_error("A file is already open\n");
                fd_ = ::open(path, O_RDONLY);
                
                if (fd_ < 0)
                {
                    if (errno == ENONET) {
                        throw std::runtime_error("no file exists\n");
                    }
                }

                is_open_ = true;
            }

            void wopen(const char* path)
            {
                if (is_set()) throw std::logic_error("A file is already open\n");
                fd_ = ::open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);

                if (fd_ < 0)
                {
                    // handle errno (ENONENT, EACCES ...)
                }

                is_open_ = true;

            }

            void close()
            {
                if (!is_open()) return;
                int ret = ::close(fd_);
                if (ret == -1) throw std::runtime_error("error on close\n");
                is_open_ = false;
            }

            /** @read: 
             * - when reading from a regular file, if the end of file is reached
             *   before the requested number of bytes has been read
             * - for example, if 30 bytes remain until the end of file and we try
             *   read 100 bytes, read return 30
             * - the next time we call read, it will return 0 (end of file)
             */


            ssize_t read(void *buf, std::size_t nbytes)
            {
                ssize_t n{ };
                if (is_set() && is_open()) 
                {
                    n = ::read(fd_, buf, nbytes);
                    if (n == 0) eof_ = true;
                    return n;
                }
                else { throw std::runtime_error("read() error"); }

            }
            
            ssize_t write(void *buf, std::size_t nbytes)
            {
                if (is_set() && is_open()) 
                {
                    return ::write(fd_, buf, nbytes);
                }
                else { throw std::runtime_error("write() error"); }
            }

            bool is_eof() const noexcept { return eof_; }

        private:
            int fd_{ -1 };
            bool is_open_{ false };
            bool eof_{ false };

            bool is_set() const noexcept { return !(fd_ < 0); }
            bool is_open() const noexcept { return is_open_; }
    };
}
