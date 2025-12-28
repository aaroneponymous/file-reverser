/**
 * File Reverser: Lock-Free - Reader, Worker, Writer Threads
 * 
 * @modules:
 * 
 *    @io_raii:  * RAII Wrapper over libc I/O system call wrapper functions
 *    @reverser: * Free functions provide functionality to reverse buffers in-place
 *                 * Single-Threaded Function @reverse_segment: in namespace file_reverser::util::st
 *                 * Multi-Threaded Function  @reverse_segment: in namespace file_reverser::util::mt
 *    
 *    Flow:
 * 
 *    Program Started --> main --> program_driver
 *                    --> memory_manager: allocate chunked memory for components:
 *                          --> 1. Buffers, 2. Segments & Segment Array, 3. Lock-Free Queues for ItemWrite Object
 *                    --> main thread spawns:
 *                        1. Reader
 *                           ---->> 
 *                        2. Worker
 *                        3. Writer
 * 
 * 
 *              
 * 
 */