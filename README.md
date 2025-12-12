### Write a program in C++ that takes in a file as input and reverses every line and puts it in a different file. 
#### Try to do this with as little memory footprint as possible and as fast as possible.
 - Overlap computations with I/O as much as possible
 - Multithreading
 - Chunked read and processing
 - SPSC lock-free queue between threads.


### \<iostream\>

This header is part of the Input/output library.

Including <iostream> behaves as if it defines a static storage duration object of type std::ios_base::Init, whose constructor initializes the standard stream objects if it is the first std::ios_base::Init object to be constructed, and whose destructor flushes those objects (except for cin and wcin) if it is the last std::ios_base::Init object to be destroyed.

### std::ios_base::Init
 C++ Input/output library std::ios_base 

class Init;
This class is used to ensure that the default C++ streams (std::cin, std::cout, etc.) are properly initialized and destructed. The class tracks how many instances of it are created and initializes the C++ streams when the first instance is constructed as well as flushes the output streams when the last instance is destructed.

The header <iostream> behaves as if it defines (directly or indirectly) an instance of std::ios_base::Init with static storage duration: this makes it safe to access the standard I/O streams in the constructors and destructors of static objects with ordered initialization (as long as <iostream> is included in the translation unit before these objects were defined).


std::basic_istream<CharT,Traits>::getline
 C++ Input/output library std::basic_istream 
basic_istream& getline( char_type* s, std::streamsize count );
(1)	
basic_istream& getline( char_type* s, std::streamsize count, char_type delim );
(2)	
Extracts characters from stream until end of line or the specified delimiter delim.

The first overload is equivalent to getline(s, count, widen('\n')).

Behaves as UnformattedInputFunction. After constructing and checking the sentry object, extracts characters from *this and stores them in successive locations of the array whose first element is pointed to by s, until any of the following occurs (tested in the order shown):

end of file condition occurs in the input sequence.
the next available character c is the delimiter, as determined by Traits::eq(c, delim). The delimiter is extracted (unlike basic_istream::get()) and counted towards gcount(), but is not stored.
count is non-positive, or count - 1 characters have been extracted (setstate(failbit) is called in this case).
If the function extracts no characters, ​failbit is set in the local error state before setstate() is called.

In any case, if count > 0, it then stores a null character CharT() into the next successive location of the array and updates gcount().

Notes
Because condition #2 is tested before condition #3, the input line that exactly fits the buffer does not trigger failbit.

Because the terminating character is counted as an extracted character, an empty input line does not trigger failbit.

Parameters
s	-	pointer to the character string to store the characters to
count	-	size of character string pointed to by s
delim	-	delimiting character to stop the extraction at. It is extracted but not stored.
Return value
*this
