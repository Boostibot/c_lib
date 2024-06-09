This is a perpetually ongoing development C codebase I use for personal projects. It reflects my current opinions of what is desirable but that does not mean the solutions given here are the absolute best. If you have any suggestions I will be more than happy to hear them.

# Most important files
- `array.h` **(freestanding)**: Generic, type-safe array in pure C. This mostly works like `std::vector`.
- `hash_index.h` **(freestanding)**: Small and very performant hash table building block. This is not a fully fledged hash table, but just a 64 -> 62 bit hash mapping. All other tables can be implemented using this. Read the comment for more rationale.
- `platform.h` **(freestanding)**: A fully fledged platform layer supporting windows and linux. Contains code for threading, intrinsics, virtual memory, filesystem (reading, writing, listing observing changes), debug facilities (callstack capturing, printing, sandboxing) and many more.  
- `perf.h` **(freestanding)**: Small set of functions for timing/benchmarking code and evaluating results. Reports average times, standard deviation, min max times and more. Has additional atomic interface for measuring multithreaded code.
- `random.h` **(freestanding)**: Convenient fast, non-cryptographic random number generation. Has both global state and local state interface.
- `path.h`: Robust path parsing, normalization and mutation algorithms. Correctly parses linux and all kinds of strange windows paths.
- `profile.h`: Very basic atomic global profiler. Does not require any initialization. Uses `perf.h`.
- `stable_array.h`: O(1) Fast, memory efficient free-list like structure keeping stable pointers. Accessible through handles. Is suitable for storing large amounts of data or implementing SQL-like tables. 
- `allocator.h`: Interface for generic allocators with special fast path for arenas.
- `arena.h` **(freestanding)**: "Safe" arena implementation. Works like regular arena but contains code that cheaply check & prevents bad usage. 
- `allocator_debug.h`: Wrapper around generic allocator that verifies correct handling and detects leaks. Can capture callstack to print exactly where the leak occurred.
- `pool_allocator.h`: A fully general allocator on top of a given memory block. All operations are hard O(1). Behaves like malloc but gives me much more control. Is only about 25% faster then malloc (to be improved).
- `math.h` **(freestanding)**: Float vector math operations.
  
# Code structure
The codebase is structured in the "stb" style, that is both `.h` and `.c` 'files' are within the same physical file. This makes just about everything simpler. To include a declaration simply include the given file. Files can be included any number of times. To pull in the implementation define before including `#define JOT_[FILE]_IMPL` or `#define JOT_ALL_IMPL` to include implementation of all files. Unlike the stb libraries this can also be done multiple times within single compilation unit (we simply add separate include guards for the declarations and implementation). This means for simple single compilation projects its sufficient to `#define JOT_ALL_IMPL` at the very top of the main file and proceed without worrying about anything else! The benefits are:
- Ease of integration: No matter the project, using this code is easy. The integration to unity build projects is seamless. The integration to multiple compilation unit projects can be done by creating a `.c` file and including all implementations there. 
- Locality of reference: Having all the code at one place is comfy. Recently I have also started including the test code into the same file as well. To pull the test code define `#define JOT_[FILE]_TEST` or `#define JOT_ALL_TEST`.
- Ease of sharing: Its far easier to share one file than sharing 2 or 3 when counting tests. This is especially valuable for "freestanding" files.

Some files are marked as "freestanding". This means they can be compiled without including any other file from this repository. However even those files can have conditional integration with the rest of the codebase. Usually this is to use `malloc` when compiling separately but allow arbitrary allocators when compiling within the rest of the repository.

Most files contain a large comment at the top giving a high level overview of: 
1. What is this code actually doing
2. What problems is it trying to solve
3. Why has the implementation chosen to be like this and not some other way
4. Specific tricky implementation details and oddities
This helps me explain my reasoning and invites others to understand and thus be able to help me improve the code. If you are not doing this style of commenting, please join me on this. The world could use more of this.

# Goals
The main goal of this project is to develop a fully sufficient environment in which I can be productive. Additional goal is to be as transparent as possible and help others learn more. Essentially "what I would liked to have when I was figuring this out". The most important qualities are in order:
1. **Simplicity**: As both the total number of meaningful lines of code and number of instructions the processor fundamentally needs to execute for the desired effect to happen
2. **Hardware informed**: The code must be designed in an amenable way to what the hardware is good at. This does not mean being necessarily optimal in terms of performance (as that is impossible for generic code such as this) but rather not being oblivious to the machine. 
3. **Observability**: It must be easy to observe what the code is doing, what is should be doing at any point in time. It must be trivial to inspect, print, debug and change.
4. **Trustworthy**: The code needs to be properly tested and hard invariants about behavior need to be formulated and enforced.
