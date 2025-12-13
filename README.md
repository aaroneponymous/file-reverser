### Write a program in C++ that takes in a file as input and reverses every line and puts it in a different file. 
#### Try to do this with as little memory footprint as possible and as fast as possible.
 - Overlap computations with I/O as much as possible
 - Multithreading
 - Chunked read and processing
 - SPSC lock-free queue between threads.

Iostream Objects
To minimize the time you have to wait on the compiler, it's good to only include the headers you really need. Many people simply include <iostream> when they don't need to -- and that can penalize your runtime as well.

