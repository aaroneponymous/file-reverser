
/**
 * @concept: Bounded Memory Block for Varying Number of Distinct Objects
 * 
 * @roles:
 * 
 * 1. Memory allocator: allocate at least the total size required by all the objects
 * 
                * due to memory alignment constraints, cache locality, flase sharing, 
                *  the memory allocated could be more than the total size of all the
                *  objects needed
 * 
 * 2. Memory Manager:
 * 
                * carve memory and hand over for object construction or when needed
 * 
 * 3. I/O System Call Wrapper:  
 *
                * an raii style wrapper over i/o system calls:
                *  open, close, read, write
                * 
                * wrapper that has open file descriptor
                * 
                * buffer* with all the offsets passed for
                *   either read or write
                * 

 *  Unbuffered I/O : 

        * provided by the functions open, read, write, lseek, and close
        * functions all work with file descriptors
 * 
 * 4. Buffer Descriptor Structs:
 * 
 * 
                * std::byte* buff_ptr;
                * std::uint
 * 
 * 
 * 
 * -
 * 
 * 
 *    
 */
