#ifndef JOT_ALLOCATOR_TLSF
#define JOT_ALLOCATOR_TLSF

// An implementation of TLSF style allocator.
// See https://ieeexplore.ieee.org/document/528746/ [T. Ogasawara, "An algorithm with constant execution time for dynamic storage allocation,"] for a paper introducing this type of allocator.
// See https://github.com/sebbbi/OffsetAllocator/tree/main for a similar and simpler implementation (thus probably easier to understand).
// 
// The premise is: We have a contiguous block of memory and we want to place allocations into it while wasting as little space as possible.
// This arises in many situations most of which are some sort of caching system. We have N dynamically sized items in a cache, 
// upon addition of a new item we pick the *least recently used*, deallocate it and allocate a new item. Because of the least recently used replacement
// policy the deallocated item is essentially chosen at random. This means simple linear allocators are not sufficient anymore. On the other hand
// malloc might not be ideal as we are now unsure of where the data for each item resides, and if the allocation will succeed. With this style of allocator
// we own the memory, can move it around, know the maximum size we are allowed to use and recompact it or grow it should we need to. It gives more control.
// 
// One additional important aspect of this allocator is that it does not need to touch the memory from which we are allocating. This means we can specify just
// the size of the backing memory block but do not give a pointer to it. This can be used to allocate from different memory such as GPU buffers.
// In fact there are two interface to this allocator:
//  1. tlsf_allocate() / tlsf_deallocate() meant for allocating in foreign (GPU) memory.
//  2. tlsf_malloc() / tlsf_free() meant for allocating in user memory. These require a memory block to be given.
//
// The allocation algorithm:
//     We keep an array of nodes each capable of representing a single allocation. 
//     The currently unneeded nodes form a freelist.
//     Used nodes contain offset of the allocation in the memory block and its size. 
//     They also (implicitly) contain how much free memory is in this node. 
//     Additionally we keep a doubly linked list of used nodes "in memory order" which is sorted 
//     by the allocation's address. Lastly we keep an array of "bins" where each bin holds 
//     doubly linked list of nodes with amount of free memory in the bins size range. 
//     The size ranges of bins are roughly exponentially distributed. 
//     The bins can be empty (there is no node with free memory in the bins range).
//     We track which bins are empty  with a series of "bin masks" (bitfields).
// 
//  0. Obtain requested size and alignment as parameters.
//  1. Use size to efficiently calculate the minimum bin index into which to the allocation fits.
//  2. Scan the bin masks starting from the minimum bin index to find bin into which to place the allocation.
//     If no such bin exists then there is no space left and we return error.
//  3. Select the first node from the selected bin's linked list. We refer to this node as `next`. 
//  4. Obtain `next`s previous node `prev` in memory order. Get unused `node` from the node freelist.
//  5. Link `prev` <-> `node` <-> `next`.
//  6. Set `node`s offset to be right after `prev`s used size. Set nodes size to the requested size.
//  7. Shrink the amount of free memory in the `next` node. This might involve relinking it to 
//     a different bin if the new free size does not match the bins range.
//  8. Return the `node` and its offset 
//
// The deallocation algorithm:
//  0. Obtain a `node` (via its index in the nodes array)
//  1. Obtains `node`s previous node `prev` and next node `next` in memory order.
//  2. Unlink `node` from memory order list. Thus `prev` <-> `next` 
//  3. If `node` has free space remove it from its bins linked list.
//  4. Increase the amount of free memory in the `next` node. This might involve relinking it to 
//     a different bin if the new free size does not match the bins range.
// 
// All of the steps outlined above are constant time thus both operations are O(1). 
// The only step which might not be is the search for appropriate sized bucket, however
// it is implemented using the ffs (find first set bit) instruction to do this in (very fast)
// constant time. We use 256 bins which can be tracked by 4 64-bit masks. This means we need to do
// at maximum 4 ffs operations, but usually just one 
// (ffs has throughput of 1 and latency in range [8,3] cycles - very fast).  
// 
//
// How to assign bin to a size? ======================================================
// 
// We want to efficiently map a size [1, 2^32-1] to the 256 bins so that we minimize wasted memory. 
// We would like the max proportional error for the given bin 
//    `error := max{(max{bin} - size)/max{bin} | size in bin}` to be as small as possible. 
// We also want the max error to be the same regardless of the bin size. This necessitates the bin sizes to be 
// exponentially distributed ie max{bin_n} = beta^n. Thus we can calculate which bucket a 
// given size belongs to by bin_index(size) := floor(log_beta(size)) = floor(log2(size)/log2(beta)).
// 
// Now we want to choose beta so that 
//     256 = bin_index(2^32) = floor(log2(2^32)/log2(beta)) = floor(32/log2(beta))
// This implies beta = 2^(1/8). Now the expression becomes 
//    bin_index(size) = floor(log2(size)/(1/8)) = floor(8*log2(size)) = floor(log2(size^8))
// This is the best answer that minimizes the error. However its very expensive to calculate. 
// Thus we need to choose a simpler approach that keeps some of the properties of the original.
// Clearly we want to keep the exponential nature to at least some level. 
// We take advantage of the fact that floor(log2(size)) == fls(size) where fls() is the 
// "find last set bit" instruction.
// Doing just this however produces error for given bin up to 100%. We solve this by linearly 
// dividing the space between log2 sized bins. We split this space into 8 meaning the error 
// for the log2 sized bin shrinks to mere 12.5%. This mapping is exactly floating point representation
// of number with 5 bits of exponent and 3 bits of mantissa. 
// For the implementation see tlsf_bin_index_from_size below. 
//
//
// Some other implementation notes ======================================================
//
// - The implementation is a bit different to the one in https://github.com/sebbbi/OffsetAllocator/tree/main 
//   and presumably other TLSF allocators. These implementations have three possible states a node can be:
//   1) fully used 2) fully unused (representing to be filled space) and 3) part of the node free list.
//   This formulation achieves a similar runtime performance but it has a few problems compared to our implementation.
//   Below is a series of diagrams showing deallocation of a node in that implementation.
//   
//   [____] means empty node
//   [####] means used node
//    <-->  means link in memory order
//     |    means link in some bins free list
// 
//               [___]                       [___]
//                 |                           |
//   [####] <--> [___] <--> [# freed #] <--> [___] <---> [####]
//                 |                           |
//               [___]                       [___]
//                              |
//                              | unlink neighbouring free nodes from the bins free list
//                              V
//                                            
//   [####] <--> [___] <--> [# freed #] <--> [___] <---> [####]
//                                            
//                               | merge neighbouring unused nodes
//                               V
//
//   [####] <--> [____________free________________] <---> [####]
//
//                              |
//                              | link the resulting merged node into a new bin list 
//                              V
//
//               [________________________________]
//                              |             
//   [####] <--> [____________free________________] <---> [####]
//                              |             
//               [________________________________]
// 
//   There are several things to note about this. First is that in the worst case (the one depicted) we have
//   to touch a total number of 11 nodes. Thats a lot of random memory accesses. Second is that after enough 
//   allocations and deallocations the resulting nodes will tend to be in similar position as the `freed` node.
//   That is surrounded on both sides by fully free nodes. If we assume N used nodes all of which are surrounded
//   by free nodes we get that there are N+1 free nodes in the system. That means that about 50% of the total node
//   capacity is wasted on free nodes.
//   
//   Now compare it with the diagram of the equivalent deallocation procedure in our implementation.
//   here [___########] means a node with some free space. The free space is proportional to the number of `_` 
//   and the filled space to the number of `#`. Again horizontal `<->` links mean in memory order linked list
//   and vertical `|` mean in bin linked list.
//
//                     [_____#########]            [________####]
//                              |                          |
//   [___########] <-> [_____##### freed ####] <-> [________############]
//                              |                          |
//                     [______################]    [_________#####]                              
//                              
//                              |  unlink from bin lists
//                              V
//
//   [___########] <-> [_____##### freed ####] <-> [________############]
// 
//                              | merge
//                              V
//   [___########] <-> [___________________________________############]
//
//                              | link to bin free list
//                              V
//                     [_________________________________######]
//                                     |
//   [___########] <-> [___________________________________############]
//                                     |
//                     [___________________________________##########]
//
//   We only end up touching 9 nodes (which is still a lot but its better) and we do not waste any memory on empty nodes
//   instead the empty spaces are represented implicitly by calculating the distance between neighbouring nodes in memory
//   order.
//
// - A big chunk of the code is dedicated to checking/asserting invariants. There are two kinds of such functions
//   one tlsf_test_[thing]_invariants() which when called test whether the structure is correct and if it is not aborts.
//   This "test" variant is available in even release builds but is not called internally. 
//   The other kind is _tlsf_check_[thing]_invariants() which is a simple wrapper around the test variant. 
//   This wrapper is used internally upon entry/exit of each function and gets turned into a noop in release builds.

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#if !defined(JOT_ALLOCATOR) && !defined(JOT_COUPLED)
    #include <assert.h>
    #include <stdlib.h>
    
    #define EXPORT
    #define INTERNAL static
    #define ASSERT(x, ...) assert(x)
    #define TEST(x, ...)  (!(x) ? abort() : (void) 0)

    typedef int64_t isize; //I have not tested it with unsigned... might require some changes
    typedef struct Allocator { int64_t dummy; } Allocator;
#endif

//The type used for offsets. 64 bit allows for full address space allocations 
// (which might be used to track virtual memory) but introduces extra 8B of needed storage
// per allocation. Can be set to 64 bit by defining TLSF_64_BIT.
#ifndef TLSF_64_BIT
    typedef uint32_t Tlsf_Size;
    #define TLSF_MAX_SIZE       UINT32_MAX
#else
    typedef int64_t Tlsf_Size;
    #define TLSF_MAX_SIZE       UINT64_MAX
#endif

#define TLSF_MIN_SIZE       8 //Minimum allowed size of allocation. Introduces internal fragmentation but helps to keep small leftover free spaced from polluting bins which makes the allocator faster
#define TLSF_BINS           (sizeof(Tlsf_Size) <= 4 ? 256 : 384)
#define TLSF_BIN_MASKS      ((TLSF_BINS + 63) / 64)
#define TLSF_MAX_ALIGN      4096 //One page 
#define TLSF_FIRST_NODE     0 //Special first node. Also serves the role of NIL value for user returns.
#define TLSF_LAST_NODE      1 //Special last node.
#define TLSF_INVALID        0xFFFFFFFF //used internally to signal missing. As user you never have to think about this.
#define TLSF_MAGIC          0x46534C54 //"TLSF" in ascii little endian. Placed before malloc blocks in debug builds to detect overflows.

#define TLSF_BIN_MANTISSA_LOG2  3
#define TLSF_BIN_MANTISSA_SIZE  ((uint32_t) 1 << TLSF_BIN_MANTISSA_LOG2)
#define TLSF_BIN_MANTISSA_MASK  (TLSF_BIN_MANTISSA_SIZE - 1)

#define TLSF_FAIL_REASON_NEED_MORE_MEMORY 1
#define TLSF_FAIL_REASON_NEED_MORE_NODES 2
#define TLSF_FAIL_REASON_UNSUPPORTED_SIZE 4 //The params were invalid. Namely the user asked for either more than TLSF_MAX_SIZE bytes or less then 0.

#define TLSF_CHECK_USED       ((uint32_t) 1 << 0)
#define TLSF_CHECK_FREELIST   ((uint32_t) 1 << 1)
#define TLSF_CHECK_BIN        ((uint32_t) 1 << 2)
#define TLSF_CHECK_DETAILED   ((uint32_t) 1 << 3)
#define TLSF_CHECK_ALL_NODES  ((uint32_t) 1 << 4)

#ifndef NDEBUG
    #define TLSF_DEBUG                      //Enables basic safety checks on passed in nodes. Adds padding to help find overwrites
    //#define TLSF_DEBUG_CHECK_DETAILED       //Enables extensive checks on nodes. 
    //#define TLSF_DEBUG_CHECK_ALL_NODES      //Checks all nodes on every entry and before return of every function. Is extremely slow and should only be used when testing this allocator
#endif

typedef struct Tlsf_Node {
    Tlsf_Size offset; //offset of the memory owned by this node. Is TLSF_INVALID when in free list. 
    Tlsf_Size size; //Size of the user requested memory of this node. This stays the same for the entire life of this node. TLSF_INVALID when in free list

    uint32_t next;  //next in order or next in free list
    uint32_t prev;  //prev in order or TLSF_INVALID when in free list

    uint32_t next_in_bin; //next in bin of this size or TLSF_INVALID when is the last node in the list or is in free list
    uint32_t prev_in_bin; //prev in bin of this size or TLSF_INVALID when is the first node in the list or is in free list
} Tlsf_Node;

typedef struct Tlsf_Allocator {
    //Allocator "virtual" interface. 
    //Just for compatibility with the rest of the lib dont worry. 
    Allocator allocator; 

    uint8_t* memory;
    isize memory_size;

    //We keep some basic stats. These are just for info.
    isize allocation_count;
    isize deallocation_count;
    isize bytes_allocated;
    isize max_bytes_allocated;
    uint32_t max_concurent_allocations;

    uint32_t node_first_free;
    uint32_t node_capacity;
    uint32_t node_count;
    Tlsf_Node* nodes;

    //The masks and first free lists for each bin. 
    // Note that based on TLSF_MIN_SIZE the first few bins might be
    // unused (about 8 of them)
    uint64_t bin_masks[TLSF_BIN_MASKS];
    uint32_t bin_first_free[TLSF_BINS];
    
    //Info about last failed allocation purely for informative reasons. 
    //Does not get reset unless another allocation fails.
    //Can be used to grow the backing memory.
    isize    last_fail_size;        //Size of the failed allocation
    //The smallest size that would need to be available sot hat the allocation would not fail.
    //That is the size of the bin the failed allocation belongs to.
    isize    last_fail_needed_size; 
    uint64_t last_fail_reason; //combination of the TLSF_FAIL_REASON_XXX flags.
} Tlsf_Allocator;

//Initializes the allocator. `memory_or_null` can be NULL in which case the allocator can only be used with the tlsf_allocate/tlsf_deallocate
// interface. Calling tlsf_malloc/tlsf_free will result in assertion.
EXPORT bool     tlsf_init(Tlsf_Allocator* allocator, void* memory_or_null, isize memory_size, void* node_memory, isize node_memory_size);
//Resets the allocator thus essentially 'freeing' all allocations.
EXPORT void     tlsf_reset(Tlsf_Allocator* allocator);
//Grows available memory
EXPORT void     tlsf_grow_memory(Tlsf_Allocator* allocator, void* new_memory, isize new_memory_size);
//Grows available node capacity
EXPORT void     tlsf_grow_nodes(Tlsf_Allocator* allocator, void* new_node_memory, isize new_node_memory_size);

//Allocates a `size` bytes of the potentially non local memory (ie. maybe on GPU) and returns an offset into the memory block. 
//Aligns the returned `offset` so that `(offset + align_offset) % align == 0`.
//Saves the allocated node handle into `node_output`. If fails to allocate returns 0 and saves 0 into `node_output`.
EXPORT isize    tlsf_allocate(Tlsf_Allocator* allocator, uint32_t* node_output, isize size, isize align, isize align_offset);
//Deallocates a node obtained from tlsf_allocate or tlsf_malloc. If node is 0 does not do anything.
EXPORT void     tlsf_deallocate(Tlsf_Allocator* allocator, uint32_t node);

//Allocates a `size` bytes in the local memory and returns a pointer to it. 
//The returned pointer `ptr` is aligned such that `((uintptr_t) ptr + align_offset) % align == 0`. 
//If fails to allocate returns NULL.
EXPORT void*    tlsf_malloc(Tlsf_Allocator* allocator, isize size, isize align, isize align_offset);
//Frees an allocation represented by a `ptr` obtained from tlsf_malloc. if `ptr` is NULL does not do anything.
EXPORT void     tlsf_free(Tlsf_Allocator* allocator, void* ptr);

//Returns the size of the given node. If the `node_i` is invalid returns 0. If the `node_i` was freed returns 0xFFFFFFFF.
EXPORT isize    tlsf_node_size(Tlsf_Allocator* allocator, uint32_t node_i);
//Returns a node of the allocation done by calling tlsf_malloc. If `ptr` is NULL returns 0. 
EXPORT uint32_t tlsf_get_node(Tlsf_Allocator* allocator, void* ptr); 

EXPORT int32_t  tlsf_bin_index_from_size(isize size, bool round_up);
EXPORT isize    tlsf_size_from_bin_index(int32_t bin_index);

//Checks whether the allocator is in valid state. If is not aborts.
// Flags can be TLSF_CHECK_DETAILED and TLSF_CHECK_ALL_NODES.
EXPORT void tlsf_test_invariants(Tlsf_Allocator* allocator, uint32_t flags);
EXPORT void tlsf_test_node_invariants(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags_or_zero, uint32_t bin_or_zero);

#endif

//=========================  IMPLEMENTATION BELOW ==================================================
#if (defined(JOT_ALL_IMPL) || defined(JOT_ALLOCATOR_TLSF_IMPL)) && !defined(JOT_ALLOCATOR_TLSF_HAS_IMPL)
#define JOT_ALLOCATOR_TLSF_IMPL

#if defined(_MSC_VER)
    #include <intrin.h>
    INTERNAL int32_t _tlsf_find_last_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanReverse64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
    
    INTERNAL int32_t _tlsf_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
    
    #define TLSF_INLINE_NEVER  __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    INTERNAL int32_t _tlsf_find_last_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        return 64 - __builtin_ctzll((unsigned long long) num) - 1;
    }
    
    INTERNAL int32_t _tlsf_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        return __builtin_ffsll((long long) num) - 1;
    }

    #define TLSF_INLINE_NEVER   __attribute__((noinline))
#else
    #error unsupported compiler!
#endif


INTERNAL bool _tlsf_is_pow2_or_zero(isize val)
{
    uint64_t uval = (uint64_t) val;
    return (uval & (uval-1)) == 0;
}

INTERNAL uint64_t _tlsf_align_up(uint64_t ptr, isize align_to)
{
    ASSERT(_tlsf_is_pow2_or_zero(align_to) && align_to > 0);
    int64_t ptr_num = (int64_t) ptr;
    ptr_num += (-ptr_num) & (align_to - 1);
    return (uint64_t) ptr_num;
}
EXPORT int32_t tlsf_bin_index_from_size(isize size, bool round_up)
{
    ASSERT(size >= 0);
    if(size < TLSF_BIN_MANTISSA_SIZE)
        return (int32_t) size;

    uint64_t usize = (uint64_t) size; 
    int32_t lower_bound_log2 = _tlsf_find_last_set_bit64(usize);
    uint32_t low_bits = (uint32_t) (usize >> (lower_bound_log2 - TLSF_BIN_MANTISSA_LOG2));
    uint32_t res = ((lower_bound_log2 - TLSF_BIN_MANTISSA_LOG2 + 1) << TLSF_BIN_MANTISSA_LOG2) | (low_bits & TLSF_BIN_MANTISSA_MASK);
    
    if(round_up)
    {
        uint32_t reconstructed = low_bits << (lower_bound_log2 - TLSF_BIN_MANTISSA_LOG2);
        res += (uint32_t) (reconstructed < usize);
    }

    return (int32_t) res;
}

EXPORT isize tlsf_size_from_bin_index(int32_t bin_index)
{
    uint32_t exp = (uint32_t) bin_index >> TLSF_BIN_MANTISSA_LOG2;
    uint32_t mantissa = (uint32_t) bin_index & TLSF_BIN_MANTISSA_MASK;
    isize size = mantissa;
    if(exp > 0)
        size = (isize) ((uint64_t) (TLSF_BIN_MANTISSA_SIZE | mantissa) << (exp - 1));

    return size;
}

INTERNAL void _tlsf_check_invariants(Tlsf_Allocator* allocator);
INTERNAL void _tlsf_check_node(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags);
INTERNAL void _tlsf_unlink_node_in_bin(Tlsf_Allocator* allocator, uint32_t node_i, int32_t bin_i);
INTERNAL void _tlsf_link_node_in_bin(Tlsf_Allocator* allocator, uint32_t node_i, int32_t bin_i);

INTERNAL isize _tlsf_allocate(Tlsf_Allocator* allocator, isize size, isize align, isize align_offset, bool align_in_memory, uint32_t* out_node)
{
    _tlsf_check_invariants(allocator);
    uint32_t bin_from = (uint32_t) tlsf_bin_index_from_size(size + align + align_offset, true);
    uint32_t bin_i = TLSF_INVALID;
    if(bin_from < TLSF_BINS)
    {
        uint32_t bin_mask_i = (uint32_t) bin_from/64;
        uint32_t bin_offset = (uint32_t) bin_from%64;
        uint64_t bin_first_overlay = ((uint64_t) 1 << bin_offset) - 1;
        uint64_t bin_first_mask = allocator->bin_masks[bin_mask_i] & ~bin_first_overlay;
        if(bin_first_mask)
        {
            int32_t found_offset = _tlsf_find_first_set_bit64(bin_first_mask);
            bin_i = bin_mask_i*64 + found_offset;
        }
        else
        {
            for(bin_mask_i++; bin_mask_i < TLSF_BIN_MASKS;bin_mask_i++)
            {
                if(allocator->bin_masks[bin_mask_i])
                {
                    bin_i = bin_mask_i*64 + _tlsf_find_first_set_bit64(allocator->bin_masks[bin_mask_i]);
                    break;
                }
            }
        }
    }

    if(bin_i == TLSF_INVALID || allocator->node_first_free == TLSF_INVALID)
    {
        allocator->last_fail_size = size;
        allocator->last_fail_needed_size = tlsf_size_from_bin_index(bin_from);
        allocator->last_fail_reason = 0;
        if(bin_from > TLSF_BINS || size + align + align_offset < 0)
            allocator->last_fail_reason = TLSF_FAIL_REASON_UNSUPPORTED_SIZE;
        else
        {
            allocator->last_fail_reason |= bin_i == TLSF_INVALID                      ? TLSF_FAIL_REASON_NEED_MORE_MEMORY : 0;
            allocator->last_fail_reason |= allocator->node_first_free == TLSF_INVALID ? TLSF_FAIL_REASON_NEED_MORE_NODES : 0;
        }
        *out_node = 0;
        return 0;
    }

    uint32_t next_i = allocator->bin_first_free[bin_i];
    _tlsf_check_node(allocator, next_i, TLSF_CHECK_USED);

    Tlsf_Node* __restrict next = &allocator->nodes[next_i]; 
    Tlsf_Node* __restrict node = &allocator->nodes[allocator->node_first_free];
    Tlsf_Node* __restrict prev = &allocator->nodes[next->prev];
    
    uint32_t prev_i = next->prev;
    uint32_t node_i = allocator->node_first_free;
    
    ASSERT(prev_i != node_i && node_i != next_i && next_i != prev_i);
    allocator->node_first_free = node->next;

    Tlsf_Size a_offset = (Tlsf_Size) align_offset;
    Tlsf_Size prev_end = prev->offset + prev->size;
    if(align_in_memory)
        node->offset = (Tlsf_Size) ((uint8_t*) _tlsf_align_up((uint64_t) allocator->memory + prev_end + a_offset, align) - allocator->memory) - a_offset;
    else
        node->offset = (Tlsf_Size) _tlsf_align_up((uint64_t) (prev_end + a_offset), align) - a_offset;
        
    node->size = (Tlsf_Size) size;
    node->next_in_bin = TLSF_INVALID; 
    node->prev_in_bin = TLSF_INVALID;
    node->next = next_i;
    node->prev = prev_i;

    next->prev = node_i;
    prev->next = node_i;
    
    ASSERT(node->offset >= prev->offset + prev->size);
    Tlsf_Size mew_node_unused = node->offset - (prev->offset + prev->size);
    //Can only happen if align > TLSF_MIN_SIZE
    if(mew_node_unused >= TLSF_MIN_SIZE) 
    {
        int32_t new_node_bin = tlsf_bin_index_from_size(mew_node_unused, false);
        _tlsf_link_node_in_bin(allocator, node_i, new_node_bin);
    }

    Tlsf_Size old_next_unused = next->offset - (prev->offset + prev->size); (void) old_next_unused;
    Tlsf_Size new_next_unused = next->offset - (node->offset + node->size);
    ASSERT(next->offset >= node->offset + node->size);

    _tlsf_unlink_node_in_bin(allocator, next_i, bin_i);
    next->next_in_bin = TLSF_INVALID;
    next->prev_in_bin = TLSF_INVALID;

    if(new_next_unused >= TLSF_MIN_SIZE)
    {
        int32_t new_next_bin = tlsf_bin_index_from_size(new_next_unused, false);
        _tlsf_link_node_in_bin(allocator, next_i, new_next_bin);
    }
    
    allocator->node_count += 1;
    allocator->allocation_count += 1;
    if(allocator->max_concurent_allocations < allocator->allocation_count - allocator->deallocation_count)
        allocator->max_concurent_allocations = (uint32_t) (allocator->allocation_count - allocator->deallocation_count);

    allocator->bytes_allocated += size;
    if(allocator->max_bytes_allocated < allocator->bytes_allocated)
        allocator->max_bytes_allocated = allocator->bytes_allocated;

    _tlsf_check_invariants(allocator);
        
    *out_node = node_i;
    return node->offset;
}

EXPORT void tlsf_deallocate(Tlsf_Allocator* allocator, uint32_t node_i)
{
    ASSERT(allocator);
    _tlsf_check_invariants(allocator);
    if(node_i == 0)
        return;
        
    ASSERT(allocator->node_count > 0);
    ASSERT(allocator->allocation_count > allocator->deallocation_count);
    _tlsf_check_node(allocator, node_i, TLSF_CHECK_USED);
    Tlsf_Node* __restrict node = &allocator->nodes[node_i]; 

    uint32_t next_i = node->next;
    uint32_t prev_i = node->prev;
    
    Tlsf_Node* __restrict next = &allocator->nodes[next_i];
    Tlsf_Node* __restrict prev = &allocator->nodes[prev_i];
    
    Tlsf_Size node_unused = node->offset - (prev->offset + prev->size);
    if(node_unused >= TLSF_MIN_SIZE)
    {
        int32_t node_bin_i = tlsf_bin_index_from_size(node_unused, false); 
        _tlsf_unlink_node_in_bin(allocator, node_i, node_bin_i);
    }
    
    ASSERT(next->offset > prev->offset + prev->size);
    Tlsf_Size old_next_unused = next->offset - (node->offset + node->size);
    Tlsf_Size new_next_unused = next->offset - (prev->offset + prev->size);
    
    if(old_next_unused >= TLSF_MIN_SIZE)
    {
        int32_t old_next_bin = tlsf_bin_index_from_size(old_next_unused, false);
        _tlsf_unlink_node_in_bin(allocator, next_i, old_next_bin);
    }
    next->next_in_bin = TLSF_INVALID;
    next->prev_in_bin = TLSF_INVALID;
    if(new_next_unused >= TLSF_MIN_SIZE)
    {
        int32_t new_next_bin = tlsf_bin_index_from_size(new_next_unused, false); 
        _tlsf_link_node_in_bin(allocator, next_i, new_next_bin);
    }

    node->next = allocator->node_first_free;
    allocator->node_first_free = node_i;
    next->prev = prev_i;
    prev->next = next_i;
    
    allocator->node_count -= 1;
    ASSERT(allocator->bytes_allocated >= node->size);
    allocator->deallocation_count += 1;
    allocator->bytes_allocated -= node->size;
    
    node->offset = TLSF_INVALID;
    #ifdef TLSF_DEBUG
        node->prev = TLSF_INVALID;
        node->size = TLSF_INVALID;
        node->prev_in_bin = TLSF_INVALID;
        node->next_in_bin = TLSF_INVALID;
    #endif
    
    _tlsf_check_invariants(allocator);
}

INTERNAL void _tlsf_unlink_node_in_bin(Tlsf_Allocator* allocator, uint32_t node_i, int32_t bin_i)
{
    ASSERT(bin_i < TLSF_BINS);
    ASSERT(node_i < allocator->node_capacity);

    Tlsf_Node* node = &allocator->nodes[node_i];
    if(node->prev_in_bin == TLSF_INVALID)
    {
        uint32_t* first_free = &allocator->bin_first_free[bin_i];
        ASSERT(node_i == *first_free);

        *first_free = node->next_in_bin;
        if(*first_free == TLSF_INVALID)
            allocator->bin_masks[bin_i/64] &= ~((uint64_t) 1 << (bin_i%64)); 
    }
    else
    {
        Tlsf_Node* prev_in_bin = &allocator->nodes[node->prev_in_bin];
        prev_in_bin->next_in_bin = node->next_in_bin;
    }
    
    if(node->next_in_bin != TLSF_INVALID)
    {
        Tlsf_Node* next_in_bin = &allocator->nodes[node->next_in_bin];
        next_in_bin->prev_in_bin = node->prev_in_bin;
    }
}

INTERNAL void _tlsf_link_node_in_bin(Tlsf_Allocator* allocator, uint32_t node_i, int32_t bin_i)
{
    ASSERT(bin_i < TLSF_BINS);
    ASSERT(node_i < allocator->node_capacity);

    Tlsf_Node* node = &allocator->nodes[node_i];
    uint32_t* first_free = &allocator->bin_first_free[bin_i];
    node->next_in_bin = *first_free;
    node->prev_in_bin = TLSF_INVALID;

    if(*first_free != TLSF_INVALID)
    {
        Tlsf_Node* next = &allocator->nodes[*first_free];
        next->prev_in_bin = node_i;
    }
    
    *first_free = node_i;
    allocator->bin_masks[bin_i/64] |= (uint64_t) 1 << (bin_i%64); 
}

EXPORT void tlsf_grow_memory(Tlsf_Allocator* allocator, void* new_memory, isize new_memory_size)
{
    _tlsf_check_invariants(allocator);
    ASSERT(new_memory_size >= allocator->memory_size && (new_memory != NULL || allocator->memory == NULL));
    
    //copy over allocation memory (if both are present and the pointer changed)
    if(new_memory && allocator->memory && new_memory != allocator->memory)
        memmove(new_memory, allocator->memory, allocator->memory_size);
    allocator->memory = (uint8_t*) new_memory;

    //Relink the end node to account for added space
    Tlsf_Node* __restrict end = &allocator->nodes[TLSF_LAST_NODE]; 
    Tlsf_Node* __restrict prev = &allocator->nodes[end->prev];

    Tlsf_Size old_end_unused = end->offset - (prev->offset + prev->size);
    if(old_end_unused >= TLSF_MIN_SIZE)
    {
        int32_t end_bin_i = tlsf_bin_index_from_size(old_end_unused, false); 
        _tlsf_unlink_node_in_bin(allocator, TLSF_LAST_NODE, end_bin_i);
    }

    end->prev_in_bin = TLSF_INVALID;
    end->next_in_bin = TLSF_INVALID;
    end->offset = (Tlsf_Size) new_memory_size;

    Tlsf_Size new_end_unused = end->offset - (prev->offset + prev->size);
    if(new_end_unused >= TLSF_MIN_SIZE)
    {
        int32_t end_bin_i = tlsf_bin_index_from_size(new_end_unused, false); 
        _tlsf_link_node_in_bin(allocator, TLSF_LAST_NODE, end_bin_i);
    }
    
    allocator->memory_size = end->offset;
    _tlsf_check_invariants(allocator);
}

EXPORT void tlsf_grow_nodes(Tlsf_Allocator* allocator, void* new_node_memory, isize new_node_memory_size)
{
    _tlsf_check_invariants(allocator);

    isize new_node_capacity = new_node_memory_size / sizeof(Tlsf_Node);
    ASSERT(new_node_capacity >= allocator->node_capacity && new_node_memory != NULL);

    //copy over node memory
    if(new_node_memory != allocator->nodes)
        memmove(new_node_memory, allocator->nodes, allocator->node_capacity*sizeof(Tlsf_Node));
    allocator->nodes = (Tlsf_Node*) new_node_memory;

    //Add the added nodes to freelist
    for(uint32_t i = (uint32_t) new_node_capacity; i-- > allocator->node_capacity;)
    {
        Tlsf_Node* node = &allocator->nodes[i]; 
        node->next = allocator->node_first_free;
        allocator->node_first_free = i;

        node->prev = TLSF_INVALID;
        node->next_in_bin = TLSF_INVALID;
        node->prev_in_bin = TLSF_INVALID;
        node->size = TLSF_INVALID;
        node->offset = TLSF_INVALID;
    }

    allocator->node_capacity = (uint32_t) new_node_capacity;
    _tlsf_check_invariants(allocator);
}

#ifdef JOT_ALLOCATOR
    INTERNAL void* _tlsf_allocator_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        Tlsf_Allocator* allocator = (Tlsf_Allocator*) (void*) self;
        void* new_ptr = tlsf_malloc(allocator, new_size, align, 0);
        if(new_ptr && old_ptr)
            memcpy(new_ptr, old_ptr, old_size < new_size ? old_size : new_size);

        if(old_ptr)
            tlsf_free(allocator, old_ptr);
        
        return new_ptr;
    }

    INTERNAL Allocator_Stats _tlsf_allocator_get_stats(Allocator* self)
    {
        Tlsf_Allocator* allocator = (Tlsf_Allocator*) (void*) self;
        Allocator_Stats stats = {0};
        stats.type_name = "Tlsf_Allocator";
        stats.is_top_level = false;
        stats.allocation_count = allocator->allocation_count;
        stats.deallocation_count = allocator->deallocation_count;
        stats.bytes_allocated = allocator->bytes_allocated;
        stats.max_bytes_allocated = allocator->max_bytes_allocated;
        stats.max_concurent_allocations = allocator->max_concurent_allocations;
        return stats;
    }   
#endif

EXPORT bool tlsf_init(Tlsf_Allocator* allocator, void* memory_or_null, isize memory_size, void* node_memory, isize node_memory_size)
{
    ASSERT(allocator);
    ASSERT(memory_size >= 0);
    memset(allocator, 0, sizeof *allocator);   

    isize node_capacity = node_memory_size / sizeof(Tlsf_Node);
    if(node_memory == NULL || node_capacity < 2)
        return false;
        
    allocator->nodes = (Tlsf_Node*) node_memory;
    allocator->memory = (uint8_t*) memory_or_null;
    allocator->memory_size = memory_size;
    allocator->node_capacity = (uint32_t) node_capacity;
    allocator->node_count = 0;
    
    #ifdef JOT_ALLOCATOR
        allocator->allocator.allocate = _tlsf_allocator_reallocate;
        allocator->allocator.get_stats = _tlsf_allocator_get_stats;
    #endif
    
    memset(allocator->nodes, TLSF_INVALID, (size_t) node_capacity*sizeof(Tlsf_Node));
    memset(allocator->bin_first_free, TLSF_INVALID, sizeof allocator->bin_first_free);

    allocator->node_first_free = TLSF_INVALID;
    for(uint32_t i = allocator->node_capacity; i-- > 0;)
    {
        Tlsf_Node* node = &allocator->nodes[i]; 
        node->next = allocator->node_first_free;
        allocator->node_first_free = i;
    }

    //Push FIRST and LAST nodes
    uint32_t first_i = allocator->node_first_free;
    Tlsf_Node* first = &allocator->nodes[first_i]; 
    allocator->node_first_free = first->next;
    
    uint32_t last_i = allocator->node_first_free;
    Tlsf_Node* last = &allocator->nodes[last_i]; 
    allocator->node_first_free = last->next;
    
    //Push first node
    ASSERT(first_i == TLSF_FIRST_NODE);
    ASSERT(last_i == TLSF_LAST_NODE);

    first->prev = TLSF_INVALID;
    first->next = TLSF_LAST_NODE;
    first->next_in_bin = TLSF_INVALID;
    first->prev_in_bin = TLSF_INVALID;
    first->offset = 0;
    first->size = 0;
    
    last->prev = TLSF_FIRST_NODE;
    last->next = TLSF_INVALID;
    last->next_in_bin = TLSF_INVALID;
    last->prev_in_bin = TLSF_INVALID;
    last->offset = (Tlsf_Size) allocator->memory_size;
    last->size = 0;

    if(last->offset >= TLSF_MIN_SIZE)
    {
        int32_t end_bin_i = tlsf_bin_index_from_size(last->offset, false);
        _tlsf_link_node_in_bin(allocator, TLSF_LAST_NODE, end_bin_i);
    }
    allocator->node_count = 2;
    
    _tlsf_check_invariants(allocator);
    return true;
}

EXPORT void tlsf_reset(Tlsf_Allocator* allocator)
{
    tlsf_init(allocator, allocator->memory, allocator->memory_size, allocator->nodes, allocator->node_capacity);
}

EXPORT isize tlsf_allocate(Tlsf_Allocator* allocator, uint32_t* node_output, isize size, isize align, isize align_offset)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(align_offset >= 0);
    ASSERT(node_output != NULL);
    ASSERT(_tlsf_is_pow2_or_zero(align) && align > 0);

    if(size == 0)
    {
        *node_output = 0;
        return 0;
    }

    return _tlsf_allocate(allocator, size, align, align_offset, false, node_output);
}

EXPORT void* tlsf_malloc(Tlsf_Allocator* allocator, isize size, isize align, isize align_offset)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(align_offset >= 0);
    ASSERT(_tlsf_is_pow2_or_zero(align) && align > 0);
    ASSERT(allocator->memory);

    uint8_t* ptr = NULL;
    if(size > 0)
    {
        #ifdef TLSF_DEBUG
            uint32_t magic = TLSF_MAGIC;
            uint32_t node = 0;
            isize total_size = size + 3*sizeof(uint32_t);
            isize header_size = 2*sizeof(uint32_t);
            isize offset = _tlsf_allocate(allocator, total_size, align, header_size, true, &node);
            if(node != 0)
            {
                ptr = allocator->memory + offset + header_size;

                memcpy(ptr - 2*sizeof(uint32_t), &node,  sizeof(uint32_t));
                memcpy(ptr - sizeof(uint32_t),   &magic, sizeof(uint32_t));
                memset(ptr, 0x55, size);
                memcpy(ptr + size, &magic, sizeof(uint32_t));
            }
        #else
            uint32_t node = 0;
            isize offset = _tlsf_allocate(allocator, size + sizeof(uint32_t), align, sizeof(uint32_t) + align_offset, true, &node);
            if(node != 0)
            {
                ptr = allocator->memory + offset + sizeof(uint32_t);
                memcpy(ptr - sizeof(uint32_t), &node, sizeof(uint32_t));
            }
        #endif
    }
    return ptr;
}

EXPORT uint32_t tlsf_get_node(Tlsf_Allocator* allocator, void* ptr)
{
    if(ptr == NULL)
        return 0;
        
    //If Crash occurred here it most likely means you have buffer overwrite somewhere!
    uint32_t node_i = 0;
    #ifdef TLSF_DEBUG
        uint32_t magic_before = 0;
        uint32_t magic_after = 0;
        memcpy(&node_i, (uint8_t*) ptr - 2*sizeof(uint32_t), sizeof(uint32_t));
        memcpy(&magic_before, (uint8_t*) ptr - sizeof(uint32_t), sizeof(uint32_t));

        ASSERT(magic_before == TLSF_MAGIC);
        Tlsf_Node* node = &allocator->nodes[node_i];
        ASSERT((TLSF_LAST_NODE < node_i && node_i < allocator->node_capacity));
        ASSERT(node->offset <= allocator->memory_size);

        memcpy(&magic_after, allocator->memory + node->offset + node->size - sizeof(uint32_t), sizeof(uint32_t));
        ASSERT(magic_after == TLSF_MAGIC);
    #else
        (void) allocator;
        memcpy(&node_i, (uint8_t*) ptr - sizeof(uint32_t), sizeof(uint32_t));
    #endif
    return node_i;
}

EXPORT void tlsf_free(Tlsf_Allocator* allocator, void* ptr)
{
    uint32_t node = tlsf_get_node(allocator, ptr);
    tlsf_deallocate(allocator, node);
}

EXPORT isize tlsf_node_size(Tlsf_Allocator* allocator, uint32_t node_i)
{
    if(TLSF_LAST_NODE < node_i && node_i < allocator->node_capacity)
    {
        Tlsf_Node* node = &allocator->nodes[node_i];
        ASSERT(node->offset <= allocator->memory_size);
        return node->size;
    }
    else
        return 0;
}

EXPORT void tlsf_test_node_invariants(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags, uint32_t bin_i)
{
    TEST(0 <= node_i && node_i < allocator->node_capacity);
    Tlsf_Node* node = &allocator->nodes[node_i];
    
    bool node_is_free = node->offset == TLSF_INVALID;
    if(flags & TLSF_CHECK_USED)
        TEST(node_is_free == false);
    if(flags & TLSF_CHECK_FREELIST)
        TEST(node_is_free);

    if(node_is_free)
    {
        #ifdef TLSF_DEBUG
            TEST(node->offset == TLSF_INVALID);
            TEST(node->prev == TLSF_INVALID);
            TEST(node->size == TLSF_INVALID);
        #endif
    }
    else
    {
        TEST(node->offset <= allocator->memory_size);
        TEST(node->prev < allocator->node_capacity || node_i == TLSF_FIRST_NODE);
        TEST(node->next < allocator->node_capacity || node_i == TLSF_LAST_NODE);
        TEST(node->size > 0 || node_i == TLSF_FIRST_NODE || node_i == TLSF_LAST_NODE);
        TEST(node->next != node_i);
        TEST(node->prev != node_i);
        if(flags & TLSF_CHECK_DETAILED)
        {
            if(node->prev_in_bin != TLSF_INVALID)
                TEST(allocator->nodes[node->prev_in_bin].next_in_bin == node_i);
                
            if(node->next_in_bin != TLSF_INVALID)
                TEST(allocator->nodes[node->next_in_bin].prev_in_bin == node_i);
            if(node_i != TLSF_LAST_NODE)
            {
                Tlsf_Node* next = &allocator->nodes[node->next];
                TEST(next->prev == node_i);
                TEST(node->offset <= next->offset);
                int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
            }
        
            if(node_i != TLSF_FIRST_NODE)
            {
                Tlsf_Node* prev = &allocator->nodes[node->prev];
                TEST(prev->next == node_i);
                TEST(prev->offset <= node->offset);
                Tlsf_Size node_portion = node->offset - (prev->offset + prev->size);
                if(node_portion == 0)
                {
                    TEST(node->prev_in_bin == TLSF_INVALID);
                    TEST(node->next_in_bin == TLSF_INVALID);
                    int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
                }
                
                uint32_t calculated_bin = TLSF_INVALID;
                if(node_portion >= TLSF_MIN_SIZE) 
                {
                    calculated_bin = tlsf_bin_index_from_size(node_portion, false);
                    TEST(allocator->bin_first_free[calculated_bin] != TLSF_INVALID);
                }

                if(flags & TLSF_CHECK_BIN)
                {
                    TEST(bin_i == calculated_bin);
                    int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
                }
            }
        }
    }
}

EXPORT void tlsf_test_invariants(Tlsf_Allocator* allocator, uint32_t flags)
{
    //Check fields
    TEST(allocator->nodes != NULL);
    TEST(allocator->node_count <= allocator->node_capacity);
    TEST(allocator->deallocation_count <= allocator->allocation_count);
    TEST(allocator->allocation_count - allocator->deallocation_count <= allocator->max_concurent_allocations);
    TEST(allocator->bytes_allocated <= allocator->max_bytes_allocated);
    
    //Check FIRST and LAST nodes.
    Tlsf_Node* first = &allocator->nodes[TLSF_FIRST_NODE];
    TEST(first->prev == TLSF_INVALID);
    TEST(first->next_in_bin == TLSF_INVALID);
    TEST(first->prev_in_bin == TLSF_INVALID);
    TEST(first->offset == 0);
    TEST(first->size == 0);
    
    Tlsf_Node* last = &allocator->nodes[TLSF_LAST_NODE];
    TEST(last->next == TLSF_INVALID);
    TEST(last->offset == (Tlsf_Size) allocator->memory_size);
    TEST(last->size == 0);
        
    if(flags & TLSF_CHECK_ALL_NODES)
    {
        //Check if bin free lists match the mask
        for(int32_t i = 0; i < TLSF_BINS; i++)
        {
            bool has_ith_bin = allocator->bin_first_free[i] != TLSF_INVALID;
            uint64_t ith_bit = (uint64_t) 1 << (i%64);
            TEST(!!(allocator->bin_masks[i/64] & ith_bit) == has_ith_bin);
        }

        //Check free list
        uint32_t nodes_in_free_list = 0;
        for(uint32_t node_i = allocator->node_first_free; node_i != TLSF_INVALID; nodes_in_free_list++)
        {
            tlsf_test_node_invariants(allocator, node_i, TLSF_CHECK_FREELIST | flags, 0);
            Tlsf_Node* node = &allocator->nodes[node_i];
            node_i = node->next;
        }

        //Go through all nodes in all bins and check them.
        uint32_t nodes_in_bins = 0;
        for(int32_t bin_i = 0; bin_i < TLSF_BINS; bin_i++)
        {
            uint32_t first_free = allocator->bin_first_free[bin_i];
            uint32_t in_bin_count = 0;
            for(uint32_t node_i = first_free; node_i != TLSF_INVALID; )
            {
                in_bin_count++;
                TEST(in_bin_count < allocator->node_capacity);
                tlsf_test_node_invariants(allocator, node_i, TLSF_CHECK_USED | TLSF_CHECK_BIN | flags, bin_i);
                
                Tlsf_Node* node = &allocator->nodes[node_i];
                node_i = node->next_in_bin;
            }

            nodes_in_bins += in_bin_count;
        }

        //Go through all nodes in order
        uint32_t nodes_counted = 0;
        for(uint32_t node_i = TLSF_FIRST_NODE; node_i != TLSF_INVALID; nodes_counted++)
        {
            TEST(nodes_counted < allocator->node_capacity);

            tlsf_test_node_invariants(allocator, node_i, flags, 0);

            Tlsf_Node* node = &allocator->nodes[node_i];
            node_i = node->next;
        }
        
        TEST(allocator->node_count >= nodes_in_bins);
        TEST(allocator->node_count == nodes_counted);
        TEST(allocator->node_capacity == nodes_counted + nodes_in_free_list);
        int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
    }
}

INTERNAL void _tlsf_check_node(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags)
{
    (void) allocator;
    (void) node_i;
    (void) flags;
    #ifdef TLSF_DEBUG
        #ifdef TLSF_DEBUG_CHECK_DETAILED
            flags |= TLSF_CHECK_DETAILED;
        #else
            flags &= ~TLSF_CHECK_DETAILED;
        #endif

        tlsf_test_node_invariants(allocator, node_i, flags, 0);
    #endif
}


INTERNAL void _tlsf_check_invariants(Tlsf_Allocator* allocator)
{
    (void) allocator;
    #ifdef TLSF_DEBUG
        uint32_t flags = 0;
        #ifdef TLSF_DEBUG_CHECK_DETAILED
            flags |= TLSF_CHECK_DETAILED;
        #endif
        
        #ifdef TLSF_DEBUG_CHECK_ALL_NODES
            flags |= TLSF_CHECK_ALL_NODES;
        #endif

        tlsf_test_invariants(allocator, flags);
    #endif
}

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_ALLOCATOR_TLSF_TEST)) && !defined(JOT_ALLOCATOR_TLSF_HAS_TEST)
#define JOT_ALLOCATOR_TLSF_HAS_TEST

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(arr) (isize)(sizeof(arr)/sizeof((arr)[0]))
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

void test_tlsf_alloc_unit()
{
    isize memory_size = 50*1024;
    isize node_memory_size = 1024*sizeof(Tlsf_Node);
    void* nodes = malloc(node_memory_size);

    Tlsf_Allocator allocator = {0};
    tlsf_init(&allocator, NULL, memory_size, nodes, node_memory_size);

    struct {
        uint32_t size;
        uint32_t align;
        uint32_t node;
    } allocs[4] = {
        {7, 8},
        {16, 8},
        {24, 4},
        {35, 16},
    };

    for(isize i = 0; i < ARRAY_SIZE(allocs); i++)
    {
        tlsf_test_invariants(&allocator, TLSF_CHECK_DETAILED | TLSF_CHECK_ALL_NODES);
        tlsf_allocate(&allocator, &allocs[i].node, allocs[i].size, allocs[i].align, 0);
        tlsf_test_invariants(&allocator, TLSF_CHECK_DETAILED | TLSF_CHECK_ALL_NODES);
    }
        
    for(isize i = 0; i < ARRAY_SIZE(allocs); i++)
    {
        tlsf_test_invariants(&allocator, TLSF_CHECK_DETAILED | TLSF_CHECK_ALL_NODES);
        tlsf_deallocate(&allocator, allocs[i].node);
        tlsf_test_invariants(&allocator, TLSF_CHECK_DETAILED | TLSF_CHECK_ALL_NODES);
    }

    free(nodes);
}

//tests whether the data is equal to the specified bit pattern
bool memtest(const void* data, int val, isize size)
{
    uint8_t* udata = (uint8_t*) data;
    uint8_t uval = (uint8_t)val;
    for(isize i = 0; i < size; i++)
        if(udata[i] != uval)
            return false;

    return true;
}

INTERNAL double _tlsf_clock_s()
{
    return (double) clock() / CLOCKS_PER_SEC;
}

INTERNAL isize _tlsf_random_range(isize from, isize to)
{
    if(from == to)
        return from;

    return rand()%(to - from) + from;
}

INTERNAL double _tlsf_random_interval(double from, double to)
{
    double random = (double) rand() / RAND_MAX;
    return (to - from)*random + from;
}

void test_allocator_tlsf_stress(double seconds, isize at_once)
{
    printf("[TEST]: test_allocator_tlsf_stress(seconds:%lf, at_once:%lli)\n", seconds, (long long) at_once);

    typedef struct {
        uint32_t size;
        uint32_t align;
        uint32_t node;
        uint32_t _padding;
        void* ptr;
    } Alloc;

    const isize  MAX_SIZE_LOG2 = 17; //1/8 MB = 256 KB
    const isize  MAX_ALIGN_LOG2 = 5;
    const double MAX_PERTURBATION = 0.2;
    
    isize memory_size = 256*1024*1024;
    isize node_memory_size = (at_once + 2)*sizeof(Tlsf_Node);

    void* nodes = malloc(node_memory_size);
    void* memory = malloc(memory_size);
    Alloc* allocs = (Alloc*) malloc(at_once*sizeof(Alloc));
    ASSERT(allocs != NULL);

    Tlsf_Allocator allocator = {0};
    TEST(tlsf_init(&allocator, memory, 0, nodes, 2*sizeof(Tlsf_Node)));

    isize iter = 0;
    for(double start = _tlsf_clock_s(); _tlsf_clock_s() - start < seconds;)
    {
        isize i = _tlsf_random_range(0, at_once);
        if(iter < at_once)
            i = iter;
        else
        {
            tlsf_free(&allocator, allocs[i].ptr);
            tlsf_test_invariants(&allocator, TLSF_CHECK_DETAILED | TLSF_CHECK_ALL_NODES);
        }
        
        double perturbation = 1 + _tlsf_random_interval(-MAX_PERTURBATION, MAX_PERTURBATION);
        isize random_align_shift = _tlsf_random_range(0, MAX_ALIGN_LOG2);
        isize random_size_shift = _tlsf_random_range(0, MAX_SIZE_LOG2);

        //Random exponentially distributed sizes with small perturbances.
        allocs[i].size = (int32_t)(((isize) 1 << random_size_shift) * perturbation);
        allocs[i].align = (int32_t) ((isize) 1 << random_align_shift);
        allocs[i].ptr = tlsf_malloc(&allocator, allocs[i].size, allocs[i].align, 0);
        
        //if failed grow what is necessary.
        if(allocs[i].ptr == NULL && allocs[i].size > 0)
        {
            if(allocator.last_fail_reason & TLSF_FAIL_REASON_NEED_MORE_MEMORY)
            {
                isize new_memory_size = allocator.memory_size*3/2 + allocator.last_fail_needed_size;
                if(new_memory_size > memory_size) 
                    new_memory_size = memory_size;
                printf("[TEST]: Tlsf allocator growing memory: %lli -> %lli Bytes\n", (long long) allocator.memory_size, (long long) new_memory_size);
                tlsf_grow_memory(&allocator, allocator.memory, new_memory_size);
            }

            if(allocator.last_fail_reason & TLSF_FAIL_REASON_NEED_MORE_NODES)
            {
                isize new_node_memory_size = allocator.node_capacity*sizeof(Tlsf_Node)*3/2 + 1;
                if(new_node_memory_size > node_memory_size) 
                    new_node_memory_size = node_memory_size;
                printf("[TEST]: Tlsf allocator growing nodes:  %lli -> %lli Nodes\n", (long long) allocator.node_capacity, (long long) new_node_memory_size/sizeof(Tlsf_Node));
                tlsf_grow_nodes(&allocator, allocator.nodes, new_node_memory_size);
            }
    
            if(allocator.last_fail_reason & TLSF_FAIL_REASON_UNSUPPORTED_SIZE)
            {
                printf("[TEST]: Tlsf allocator BAD PARAMS asked for %lli Bytes\n", (long long) allocator.last_fail_size);
                TEST(false);
            }

            allocs[i].ptr = tlsf_malloc(&allocator, allocs[i].size, allocs[i].align, 0);
            TEST(allocs[i].ptr != NULL);
        }
        
        allocs[i].node = tlsf_get_node(&allocator, allocs[i].ptr);

        TEST((uint64_t) allocs[i].ptr == _tlsf_align_up((uint64_t) allocs[i].ptr, allocs[i].align));
        tlsf_test_invariants(&allocator, TLSF_CHECK_DETAILED | TLSF_CHECK_ALL_NODES);

        iter += 1;
    }

    free(allocs);
    free(nodes);
    free(memory);
}

void test_allocator_tlsf(double seconds)
{
    printf("[TEST]: Tlsf allocator sizes below:\n");
    for(int32_t i = 0; i < TLSF_BINS; i++)
    {
        if(i < 50)
        {
            uint32_t this_bin_size = (uint32_t) tlsf_size_from_bin_index(i);
            uint32_t next_bin_size = (uint32_t) tlsf_size_from_bin_index(i+1);

            for(uint32_t k = this_bin_size + 1; k < next_bin_size; k++)
            {
                int32_t should_be_i = tlsf_bin_index_from_size(k, false);
                int32_t should_be_i_plus_one = tlsf_bin_index_from_size(k, true);
                TEST(should_be_i == i);
                TEST(should_be_i_plus_one == i + 1);
                TEST(should_be_i_plus_one == i + 1);
            }
        }
        printf("[TEST]: %3i -> %lli\n", i, tlsf_size_from_bin_index(i));
    }

    test_tlsf_alloc_unit();
    test_allocator_tlsf_stress(seconds/4, 1);
    test_allocator_tlsf_stress(seconds/4, 10);
    test_allocator_tlsf_stress(seconds/4, 100);
    test_allocator_tlsf_stress(seconds/4, 200);

    printf("[TEST]: test_allocator_tlsf(%lf) success!\n", seconds);
}

//Include the benchmark only when being included alongside the rest of the codebase
// since I cant be bothered to make it work without any additional includes
#ifdef JOT_ALLOCATOR
    #include "perf.h"
    #include "random.h"
    #include "log.h"
    void benchmark_allocator_tlsf_single(double seconds, bool touch, isize at_once, isize min_size, isize max_size, isize min_align_log2, isize max_align_log2)
    {
        LOG_INFO("BENCH", "Running benchmarks for %s with touch:%s at_once:%lli size:[%lli, %lli) align_log:[%lli %lli)", 
            format_seconds(seconds).data, touch ? "true" : "false", at_once, min_size, max_size, min_align_log2, max_align_log2);

        enum {
            CACHED_COUNT = 1024,
            BATCH_SIZE = 1,
        };
        typedef struct Alloc {
            void* ptr;
            uint32_t node;
            uint32_t _padding;
        } Alloc;

        typedef struct Cached_Random {
            int32_t size;
            int32_t align;
            int32_t index;
        } Cached_Random;

        enum {
            DO_ARENA,
            DO_TLSF,
            DO_MALLOC,
        };
    
        Arena arena = {0};
        TEST(arena_init(&arena, 0, 0));
        isize memory_size = 1024*1024*1024;
        arena_commit(&arena, memory_size);

        Alloc* allocs = (Alloc*) malloc(at_once * sizeof(Alloc));
        memset(allocs, -1, at_once * sizeof(Alloc));

        Cached_Random* randoms = (Cached_Random*) malloc(CACHED_COUNT * sizeof(Cached_Random));

        double warmup = seconds/10;

        for(isize i = 0; i < CACHED_COUNT; i++)
        {
            Cached_Random cached = {0};
            cached.size = (int32_t) random_range(min_size, max_size);
            cached.align = (int32_t) ((isize) 1 << random_range(min_align_log2, max_align_log2));
            cached.index = (int32_t) random_i64();

            randoms[i] = cached;
        }

        Tlsf_Allocator tlsf = {0};
        void* tlsf_memory = malloc(memory_size);
        isize node_memory_size = (at_once + 2)*sizeof(Tlsf_Node);
        void* tlsf_nodes = malloc(node_memory_size);
        tlsf_init(&tlsf, tlsf_memory, memory_size, tlsf_nodes, node_memory_size);

        Perf_Stats stats_tlsf = {0};
        Perf_Stats stats_tlsf_free = {0};
    
        Perf_Stats stats_malloc_alloc = {0};
        Perf_Stats stats_malloc_free = {0};
        
        Perf_Stats stats_arena_alloc = {0};
        Perf_Stats stats_arena_free = {0};

        for(isize j = 0; j < 3; j++)
        {
            Perf_Stats* stats_alloc = NULL;
            Perf_Stats* stats_free = NULL;
            if(j == DO_ARENA) {
                stats_alloc = &stats_arena_alloc;
                stats_free = &stats_arena_free;
            }
            
            if(j == DO_TLSF) {
                stats_alloc = &stats_tlsf;
                stats_free = &stats_tlsf_free;
            }
            
            if(j == DO_MALLOC) {
                stats_alloc = &stats_malloc_alloc;
                stats_free = &stats_malloc_free;
            }

            isize curr_batch = 0;
            isize accumulated_alloc = 0;
            isize accumulated_free = 0;
            isize failed = 0;

            isize active_allocs = 0;
            for(Perf_Benchmark bench_alloc = {0}, bench_free = {0}; ;) 
            {
                bool continue1 = perf_benchmark_custom(&bench_alloc, stats_alloc, warmup, seconds, BATCH_SIZE);;
                bool continue2 = perf_benchmark_custom(&bench_free, stats_free, warmup, seconds, BATCH_SIZE);;
                if(continue1 == false || continue2 == false)
                    break;
                    
                _tlsf_check_invariants(&tlsf);

                isize iter = bench_alloc.iter;
                Cached_Random random = randoms[iter % CACHED_COUNT];

                isize i = (isize) ((uint64_t) random.index % at_once);
                //At the start only alloc
                if(active_allocs < at_once)
                {
                    i = active_allocs;
                    active_allocs += 1;
                }
                else
                {
                    i64 before_free = perf_now();
                    if(j == DO_MALLOC) 
                        free(allocs[i].ptr);
                    if(j == DO_TLSF) 
                    {
                        //tlsf_free(&tlsf, allocs[i].ptr);
                        tlsf_deallocate(&tlsf, allocs[i].node);
                    }
                    if(j == DO_ARENA) 
                    {
                        arena_reset(&arena, 0);
                        active_allocs = 0;
                    }

                    i64 after_free = perf_now();
                    accumulated_free += after_free - before_free;
                }  

                i64 before_alloc = perf_now();
                if(j == DO_MALLOC) 
                    allocs[i].ptr = malloc(random.size);
                if(j == DO_TLSF) 
                {
                    //allocs[i].ptr = tlsf_malloc(&tlsf, random.size, random.align);
                    allocs[i].ptr = tlsf.memory + tlsf_allocate(&tlsf, &allocs[i].node, random.size, random.align, 0);
                }
                if(j == DO_ARENA) 
                    allocs[i].ptr = arena_push_nonzero(&arena, random.size, random.align);

                if(allocs[i].ptr == NULL)
                    failed += 1;
                if(touch && allocs[i].ptr)
                    memset(allocs[i].ptr, 0, random.size);
                i64 after_alloc = perf_now();
                
                if(iter >= at_once)
                    accumulated_alloc += after_alloc - before_alloc;

                if(iter >= at_once && curr_batch % BATCH_SIZE == 0)
                {
                    perf_benchmark_submit(&bench_free, accumulated_free);
                    perf_benchmark_submit(&bench_alloc, accumulated_alloc);
                    accumulated_free = 0;
                    accumulated_alloc = 0;
                }
                curr_batch += 1;
            }

            //printf("failed:%lli \n", failed);
        }
    
        free(allocs);
        free(randoms);
        free(tlsf_memory);
        free(tlsf_nodes);
        arena_deinit(&arena);

        log_perf_stats_hdr("BENCH", LOG_INFO, "ALLOC:        ");
        log_perf_stats_row("BENCH", LOG_INFO, "arena         ", stats_arena_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf          ", stats_tlsf);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc        ", stats_malloc_alloc);
        
        log_perf_stats_hdr("BENCH", LOG_INFO, "FREE:         ");
        log_perf_stats_row("BENCH", LOG_INFO, "arena         ", stats_arena_free);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf          ", stats_tlsf_free);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc        ", stats_malloc_free);
    }

    void benchmark_allocator_tlsf(bool touch, double seconds)
    {
        benchmark_allocator_tlsf_single(seconds, touch, 4096, 8, 64, 0, 4);
        benchmark_allocator_tlsf_single(seconds, touch, 1024, 64, 512, 0, 4);
        benchmark_allocator_tlsf_single(seconds, touch, 1024, 8, 64, 0, 4);
        benchmark_allocator_tlsf_single(seconds, touch, 256, 64, 512, 0, 4);
        benchmark_allocator_tlsf_single(seconds, touch, 1024, 4000, 8000, 0, 4);
    }
    #endif
#endif