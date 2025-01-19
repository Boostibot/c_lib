This is a perpetually ongoing development C codebase I use for personal projects. It reflects my current opinions of what is desirable but that does not mean the solutions given here are optimal. If you have any remarks I will be more than happy to hear them.

# Most important files
- `array.h` **(freestanding)**: Generic, type-safe array in pure C. This mostly works like `std::vector`.
- `hash.h` **(freestanding)**: Small and very performant hash table building block. This is not a fully fledged hash table, but just a 64 -> 62 bit hash mapping. All other tables can be implemented using this. Read the comment for more rationale.
- `scratch.h` **(freestanding)**: "Safe" arena implementation. Works like regular arena but contains code that cheaply check & prevents bad usage. 
- `allocator_tlsf.h`: **(freestanding)** A TLSF style allocator on top of a given memory block. All operations are hard O(1). All book-keeping is done in seperate memory, allowing interface for allocation on the GPU. Is about 25% faster then malloc.
- `allocator_debug.h`: Wrapper around generic allocator that verifies correct handling and detects leaks. Can capture callstack to print exactly where the leak occurred.
- `allocator.h`: Interface for generic allocators with special fast path for arenas.
- `platform.h` **(freestanding)**: A fully fledged platform layer supporting windows and linux. Contains code for threading, intrinsics, virtual memory, filesystem (reading, writing, listing observing changes), debug facilities (callstack capturing, printing, sandboxing) and many more.  
- `path.h`: Robust path parsing, normalization and mutation algorithms. Correctly parses linux and all kinds of strange windows paths.
- `perf.h` **(freestanding)**: Small set of functions for timing/benchmarking code and evaluating results. Reports average times, standard deviation, min max times and more. 
- `slz4.h`: **(freestanding)** Simple but quite fast LZ4 compressor/decompressor. On the enwik8 dataset achieves compression speed of 130MB/s, 2.10 compression ratio and decompression speed of 2.7GB/s. Tested for safety and full standard compliance.
- `profile.h`: Extremely low overhead averaging profiler. Uses a combination of thread local storage and lazy initialization to only require a single branch worth of overhead. Captures total runtime, runs, min/max runtime and standard deviation.
- `stable_array.h`: O(1) Fast, memory efficient free-list like structure keeping stable pointers. Accessible through handles. Is suitable for storing large amounts of data or implementing SQL-like tables. 
- `deprecated/unicode.h` **(freestanding)**: Conversion between UTF8, UTF16, UTF32 with proper error checking. Extensively tested.
- `math.h` **(freestanding)**: Float vector math operations.
- `random.h` **(freestanding)**: Convenient fast, non-cryptographic random number generation. Has both global state and local state interface.
  
# Code structure
The codebase is structured in the "stb" style, that is both `.h` and `.c` 'files' are within the same physical file. This makes just about everything simpler. To include a declaration simply include the given file. Files can be included any number of times. To pull in the implementation define before including `#define JOT_[FILE]_IMPL` or `#define JOT_ALL_IMPL` to include implementation of all files. Unlike the stb libraries this can also be done multiple times within single compilation unit (we simply add separate include guards for the declarations and implementation). This means for simple single compilation projects its sufficient to `#define JOT_ALL_IMPL` at the very top of the main file and proceed without worrying about anything else! The benefits are:
- Ease of integration: No matter the project, using this code is easy. The integration to unity build projects is seamless. The integration to multiple compilation unit projects can be done by creating a `.c` file and including all implementations there. 
- Locality of reference: Having all the code at one place is comfy. Recently I have also started including the test code into the same file as well. To pull the test code define `#define JOT_[FILE]_TEST` or `#define JOT_ALL_TEST`.
- Ease of sharing: Its far easier to share one file than sharing 2 or 3 when counting tests. This is especially valuable for "freestanding" files.

Some files are marked as "freestanding". This means they can be compiled without including any other file from this repository. However even those files can have conditional integration with the rest of the codebase. Usually this is to use `malloc` when compiling separately but allow arbitrary allocators when compiling within the rest of the repository.

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
4. **Trustworthy**: The code needs to be properly tested. Hard invariants about behavior need to be formulated and enforced.
