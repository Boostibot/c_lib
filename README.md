This is a perpetually ongoing development C codebase I use for personal projects. It reflects my current opinions of what is desirable but that does not mean the solutions given here are optimal. If you have any remarks I will be more than happy to hear them.

# Description of some files
- *`array.h`: Generic, type-safe array in pure C. This mostly works like `std::vector`.
- *`hash.h`: Small and performant hash table building block. This is not a fully fledged hash table, but just a 64 -> 64 bit hash mapping. All other tables can be implemented using this. Read the comment for more rationale.
- *`string.h`: Collection of utility hash function operating on both slice-like and dynamic strings. 
- *`scratch.h`: "Safe" arena implementation. Works like regular arena but contains code that cheaply check protect agains prevents accidental overriding of data. 
- *`random.h`: Convenient fast, non-cryptographic random number generation. Has both global state and local state interface.
- *`time.h`: Simple header for cross platform time stamps. 
- *`math.h`: Float vector math library.
- *`perf.h`: Cross platform wrappers around `rdtsc()` instruction and a construct optimized for convenient yet suprisingly accurate benchmarking.
- `path.h`: Robust path parsing, normalization and mutation algorithms. Correctly parses linux and all kinds of strange windows paths.
- `match.h`: A convenient set of functions for parsing of text and various floating point formats. The primitives are designed to be strict yet composable, making it easy to build parsers that validate compliance.
- *`platform.h`: A fully fledged platform layer supporting windows and linux. Contains code for threading, intrinsics, virtual memory, filesystem (reading, writing, listing observing changes), debug facilities (callstack capturing, printing, sandboxing) and many more.  
- *`allocator.h`: Interface for generic allocators.
- `allocator_debug.h`: Wrapper around generic allocator that verifies no overwrites and detects leaks. Has support for on demand checking of all blocks, continual printing of allocations etc. Can capture callstack to print exactly where the problematic allocation came from.
- *`allocator_tlsf.h`: A TLSF style allocator on top of a given memory block. All operations are hard O(1). All book-keeping is done in seperate memory, allowing interface for allocation on the GPU. Is about 25% faster then malloc but currently essentially usnuseed because of its complexity.
- *`deprecated/unicode.h`: Conversion between UTF8, UTF16, UTF32 with proper error checking. Extensively tested.
- *`chase_lev_queue.h`: SPMC Chase-Lev lock-free queue.
- *`stable_array.h`: O(1) Fast, memory efficient free-list like structure keeping stable pointers. Accessible through handles. Is suitable for storing large amounts of data or implementing SQL-like tables. 
- *`serialize.h`: Procedures for binary JSON-like parsing in "immediate style". That is, no tree structure is made, instead the contents are parsed as they come in. The format itself is forward and backward compatible and includes mechanism for seamless error recovery through writer defined magic numbers which are transparent to the reader.
- *`channel.h`: Novel Go-like concurrent channel. Fixed capacity MPMC ordered queue. As long as the channel is not empty/full is fully lock free on pop/push. Just like Go has procedures for closing which still allow to retrieve the stored data (this has been hard to achieve and where the novelty comes from). 
- *`image.h`: Generic image container and subimage view into it. Works with any pixel format as long as it fits evenly into some number of bytes (ie. doesnt do bitpacking). 
- *`slz4.h`: Simple but quite fast LZ4 compressor/decompressor. On the enwik8 dataset achieves compression speed of 130MB/s, 2.10 compression ratio and decompression speed of 2.7GB/s. Tested for safety and full standard compliance.
- *`sort.h`: A generic C sorting implementation (ie. like `qsort`) which abuses `__forceinline` (or similar) directive to inline the function-pointer argument to generate close to optimal assembly. Has a quick sort impelmentation that matches perf of pdqsort on random data as well as very optimized heapsort which outperforms pdqsort by about 20% on large (> 3000 items) datasets. Yes, I was surprised too - turns out heapsort is *really* fast when written properly. 
- *`wip/profile2.h`: WIP low overhead tracing profiler both in terms of runtime and assembly. All of the data processing and compression to the on disk format is done in separate thread. When runtime dissabled has essentially zero perf impact.   

Files marked with* are *completely* freestanding - they dont depend on any other file and can be compiled separately. See below for more info.



# Code structure
The codebase is structured in the "stb" style, that is both `.h` and `.c` 'files' are within the same physical file. This makes just about everything simpler. To include a declaration simply include the given file. Files can be included any number of times. To pull in the implementation define before including `#define MODULE_[FILE]_IMPL` or `#define MODULE_IMPL_ALL` to include implementation of all files. Unlike the stb libraries this can also be done multiple times within single compilation unit (we simply add separate include guards for the declarations and implementation). This means for simple single compilation projects its sufficient to `#define MODULE_IMPL_ALL` at the very top of the main file and proceed without worrying about anything else! The benefits are:
- Ease of integration: No matter the project, using this code is easy. The integration to unity build projects is seamless. The integration to multiple compilation unit projects can be done by creating a `.c` file and including all implementations there. 
- Locality of reference: Having all the code at one place is comfy. Recently I have also started including the test code into the same file as well. To pull the test code define `#define MODULE_[FILE]_TEST` or `#define MODULE_ALL_TEST`.
- Ease of sharing: Its far easier to share one file than sharing 2 or 3 when counting tests. This is especially valuable for "freestanding" files.
- Understanding: Having most files be freestanding is difficult and incurs some code duplications but it makes for much easier reading. One does not need to understand the entire codebase to udnerstand a single module. 

Most files are marked as "freestanding". This means they can be compiled without including any other file from this repository. However even those files can have conditional integration with the rest of the codebase through either defines such as `ASSERT`, `PROFILE_START` etc. or generic allocator interface (which is just a conetxtful function pointer).

Most files contain a large comment at the top giving a high level overview. This helps me explain my reasoning and invites others to understand and thus be able to help me improve the code. If you are not doing this style of commenting, please join me on this. The world could use more of this. It attempts to answer the following questions:
1. What is this code actually doing
2. What problems is it trying to solve
3. Why has the implementation chosen to be like this and not some other way
4. Specific tricky implementation details and oddities

# Goals
The main goal of this project is to develop a fully sufficient environment in which I can be productive. Additional goal is to be as transparent as possible and help others learn more. Essentially "what I would liked to have when I was figuring this out". The most important qualities are in order:
1. **Simplicity**: The total number of meaningful lines of code and/or number of instructions the processor has to execute, needs to be kept proportional to the problem complexity.
2. **Hardware informed**: The code must be designed in an amenable way to what the hardware is good at. This does not mean being necessarily optimal in terms of performance but rather not being oblivious to the machine. 
3. **Observability**: It must be easy to observe what the code is doing and what it should be doing at any point in time. It must be trivial to inspect, print, debug and change.
4. **Trustworthy**: The code needs to be properly tested. Hard invariants about behavior need to be formulated and enforced. Inputs should be validated and bounds checked. Most of these checks should be cheap enough to be enabled even in relese builds.
