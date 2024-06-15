#ifndef JOT_POOL_ALLOCATOR
#define JOT_POOL_ALLOCATOR

// An implementation of TLSF style allocator (see: "An algorithm with constant execution time for dynamic storage allocation.")
// Also see https://github.com/sebbbi/OffsetAllocator/tree/main for a similar implementation.
// 
// The allocation algorhitm can be summarised as follows:
//  0. Obtain requested size and alignment as parameters
//  1. Use size to efficiently calculate a bin into which to place the allocation.
//     Each bin contains a (circular bidirectional) linked list of free nodes.
//  2. The bin index obtained is the smallest bin into which the allocation fists. 
//     We store a bitmask of which bins have at least one ndoe on its free list.
//     Using the index mask off too small bins from the bitmask, then find first set
//     bit to get the smallest elegible bin.
//  3. Place the allocation to the first node from the bins free list.
//     Unlink the node from the free list.
//  4. If the node used is bigger then the requetsed size and there is 
//     sufficient ammount of space left, create a new node with filling
//     this space. Add it to the appropriate bins free list.
//     Additionally there is a (circular bidirectional) linked list of 
//     adress space neigbouring nodes used for splitting and merging of nodes.
//     Add the created node between the used node its neiigbour. 
//     Mark the added node as not used.
//  5. Align the allocation to the specified alignment. Place a header 
//     containing the offset to the used node. Mark it as used.
//
// The deallocation algorhitm can be summarised as follows:
//  0. Obtain a pointer to an allocated region.
//  1. Read the header before the pointer and use the specified offset to
//     lookup the node itself.
//  2. Lookup its two neighboring nodes and check if they are used. 
//     If a neighbour (does not matter which one, can merge both at once) 
//     is not used it can be merged with the deallocated node. Unlink the 
//     neighbour from its bins freelist and remove it from the adjecent list, 
//     thus merging it with the deallocated node. Increase the deallocated 
//     node's size by the merged neigbours size. 
//  3. Obtain the deallocated node's bin index and place it inside its freelist.
//     Mark it as not used.
//
// The resulting implementation is only about 25% faster than malloc and it provides 
// more control. The whole pool can be trivially deallocated at once 
// (by forgetting about it and reseting the allocator). A resizing of the memory reagion can 
// be added without too much work by putting this allocator on top of growing arena.
// 
// All of the steps otulined above are constant time thus both operations are O(1). 
// The only step which might not be is the search for appropriate sized bucket, however
// its implemented using the ffs (find first set bit) instruction to do this in (very fast)
// cosntant time. We use 64 bins because that is the biggest commonly supported size for ffs.
// 
// How to assign bin to a size? ======================================================
// 
// We want to efficiently map a size to 64 bins so that we minimize wasted memory 
// (that is we dont want to use 16KB block for 16B allocation). We would like the max error for bin
// error := max{size / max{bin} | size in bin} to be as small as possible. We also want the max
// error to be the same regardless of the bin size. This necessitates the bin sizes to be 
// exponentially distributed ie max{bin_n} = beta^n. Thus we can calculate which bucket a 
// given size belongs to by bin_index := floor(log_beta(size)) = floor(log2(size)/log2(beta)).
// 
// How to choose beta? Remember that we want 64 bins thus MAX_SIZE = max{bin_64} = beta^64, 
// which perscribes the beta for the given MAX_SIZE by beta = pow(MAX_SIZE, 1/64). 
// Now we choose MAX_SIZE so that beta is some nice number. If we choose 2^64 beta is 2. 
// This is okay but the max error is then (2^64 - 1)/2^64 approx 100%. Generally we can say
// max error is approximately beta - 1. Also supporting all  numbers is a bit wasteful if 
// the adress range after all is only 2^48 and realistically we will not use this allocator 
// for anything more than lets say 4GB of total space. A next  nice MAX_SIZE is 2^32. 
// This means beta is sqrt(2), thus 
// bin_index = floor(log2(size)/log2(sqrt(2))) = floor(log2(size)/0.5) = floor(2*log2(size)).
// This can be quite nicely calculate using a single fls (find last set) isntruction to 
// obtain floor(log2(size)) and then simple check to obtain the correct answer. (see implementation). 
// The only problem is that now MAX_SIZE is only 4GB, which we solve by introducing MIN_SIZE. 
// Simply instead of talking about individual bytes we will only refer to multiples of MIN_SIZE bytes. 
// Thus we convert size to ceil(size/MIN_SIZE), calculate the bin for that and rememeber to multiply 
// the resulting size by MIN_SIZE before presenting to the user. We choose MIN_SIZE = 8 thus 
// MAX_SIZE = 32GB. The max error is sqrt(2) - 1 = 42% thus the average error is 21%. Good enough. 
// 
// Some other implementation notes ======================================================
// 
// - The actual assignemnet to bins using the ffs/fls is very very fast and entirely covered 
//   by memory latency. One could essentially for free set n=128, thus requiring two ffs ops to 
//   find a bin (this would mean beta = 2^(1/4)). This would decrease the max error to 
//   2^(1/4) - 1 = 19% and average of just 10%. This is better then the approach used by sebbbi 
//   in the link above.
//   We would also need two times as many bins thus the allocator struct would become also 
//   twice as big, though I am unsure how much of an impact on perfomance that has. 
// 
// - I used somewhat unusual (at least for me) circular linked lists as they eliminate most ifs
//   in the algorhitm. Its surprising how much nicer everything becomes for circular vs normal 
//   doubly linked lists. 
// 
// - All numbers which are actually scalled by MIN_SIZE are labeled [thing]. The conversion 
//   to and from  is super fast as MIN_SIZE is compile time known power of 2. Even if it werent
//   the impact would be hidden by memory latency.
// 
// - As mentioned few times memory latency is the biggest bottleneck. This is best seen by going over the 
//   summary of the algorithms above and realizing what the steps entail. Each linking/unlinking requires 
//   visiting of neigboring nodes. This is esentially a random memory lookup. For the free alogrithm there is
//   1 lookup for the deallocated node
//   2 lookups to visit neighbours
//   (lets suppose we merge jsut one neighbour)
//      2 lookups to unlink neighbour from bucket free list
//      1 lookup to unlink it from the neighbours list
//   2 lookups to link to the free list
//   This is incredibly inefficient. I am wondering how to go about reducing this. Clearly we need to colocate
//   the adjecent nodes in storage somehow. Its unclear how to go about this.
// 
// - A big chunk of the code is dedicated to checking/asserting invariants. There are two functions
//   _pool_check_[thing]_always() which are kept around always and two wrappers _pool_check_[thing]()
//   which are removed in release builds. Only the removed variant is used for asserts within the functions
//   themselves. The [thing]_always variants are used during stress testing. An operation is performed and then
//   invariants are checked. This is incredibly powerful and convenient way of making sure a datstructure/algorithm 
//   is correct - setup as many invariants as one can come up with and then check them as often as possible 
//   (so that when unexpected state occurs we learn about it).

#if !defined(JOT_ALLOCATOR) && !defined(JOT_COUPLED)
    #include <stdint.h>
    #include <string.h>
    #include <assert.h>
    #include <stdbool.h>
    #include <stdlib.h>
    
    #define EXPORT
    #define INTERNAL static
    #define ASSERT(x, ...) assert(x)
    #define TEST(x, ...)  (!(x) ? abort() : (void) 0)

    typedef int64_t isize; //I have not tested it with unsigned... might require some changes
#endif

#define POOL_ALLOC_MIN_SIZE             8
#define POOL_ALLOC_MAX_SIZE             (isize) UINT32_MAX
#define POOL_ALLOC_BINS                 64
#define POOL_ALLOC_MAX_ALIGN            4096 //One page 
#define POOL_ALLOC_INVALID              0xFFFFFFFF
#define POOL_ALLOC_START                0
#define POOL_ALLOC_END                  1

#define POOL_ALLOC_CHECK_UNUSED     ((uint32_t) 1 << 0)
#define POOL_ALLOC_CHECK_USED       ((uint32_t) 1 << 1)
#define POOL_ALLOC_CHECK_FREELIST   ((uint32_t) 1 << 5)
#define POOL_ALLOC_CHECK_DETAILED   ((uint32_t) 1 << 2)
#define POOL_ALLOC_CHECK_ALL_NODES  ((uint32_t) 1 << 3)
#define POOL_ALLOC_CHECK_BIN        ((uint32_t) 1 << 4)

#ifndef NDEBUG
    //#define POOL_ALLOC_DEBUG            //Enables basic safery checks on passed in nodes. Helps to find overwrites
    //#define POOL_ALLOC_DEBUG_SLOW       //Enebles extensive checks on nodes. Also fills data to garavge on alloc/free.
    //#define POOL_ALLOC_DEBUG_SLOW_SLOW  //Checks all nodes on every entry and before return of every function. Is extremely slow and should only be used when testing this allocator
#endif

typedef struct Pool_Allocator_Node {
    uint32_t next;  //next in order or next in free list
    uint32_t prev;  //prev in order or POOL_ALLOC_INVALID when in free list

    uint32_t next_in_bin; //next in bin of this size or 0 when node in use or POOL_ALLOC_INVALID when in free list
    uint32_t prev_in_bin; //prev in bin of this size or 0 when node in use or POOL_ALLOC_INVALID when in free list

    uint32_t offset; //POOL_ALLOC_INVALID when in free list
    uint32_t size; //POOL_ALLOC_INVALID when in free list
} Pool_Allocator_Node;

typedef struct Pool_Allocator {
    uint8_t* memory;
    isize memory_size;
    
    uint32_t allocation_count;
    uint32_t max_allocation_count;
    isize bytes_allocated;
    isize max_bytes_allocated;

    uint32_t _padding;
    uint32_t node_first_free;
    uint32_t node_capacity;
    uint32_t node_count;
    Pool_Allocator_Node* nodes;

    uint64_t bin_mask;
    uint32_t bin_first_free[POOL_ALLOC_BINS];
} Pool_Allocator;

EXPORT void     pool_alloc_init(Pool_Allocator* allocator, void* memory, isize memory_size, isize node_count);
EXPORT uint32_t pool_alloc_allocate(Pool_Allocator* allocator, isize size, isize align);
EXPORT void     pool_alloc_deallocate(Pool_Allocator* allocator, uint32_t node_i);
EXPORT void     pool_alloc_reset(Pool_Allocator* allocator);

EXPORT uint32_t pool_alloc_get_node_size(Pool_Allocator* allocator, uint32_t node_i);
EXPORT uint32_t pool_alloc_get_node(Pool_Allocator* allocator, void* ptr);
EXPORT void*    pool_alloc_malloc(Pool_Allocator* allocator, isize size, isize align);
EXPORT void     pool_alloc_free(Pool_Allocator* allocator, void* ptr);

//Checks wheter the allocator is in valid state. If is not aborts.
// Flags can be POOL_ALLOC_CHECK_DETAILED and POOL_ALLOC_CHECK_ALL_NODES.
EXPORT void pool_alloc_check_invariants_always(Pool_Allocator* allocator, uint32_t flags);

#endif

//=========================  IMPLEMENTATION BELOW ==================================================
#if (defined(JOT_ALL_IMPL) || defined(JOT_POOL_ALLOCATOR_IMPL)) && !defined(JOT_POOL_ALLOCATOR_HAS_IMPL)
#define JOT_POOL_ALLOCATOR_IMPL

#if defined(_MSC_VER)
#include <intrin.h>
    inline static int32_t _pool_find_last_set_bit32(uint32_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanReverse(&out, (unsigned long) num);
        return (int32_t) out;
    }
    
    inline static int32_t _pool_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
#elif defined(__GNUC__) || defined(__clang__)
    inline static int32_t _pool_find_last_set_bit32(uint32_t num)
    {
        ASSERT(num != 0);
        return 32 - __builtin_ctz((unsigned int) num) - 1;
    }
    
    inline static int32_t _pool_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        return __builtin_ffsll((long long) num) - 1;
    }
#else
    #error unsupported compiler!
#endif


INTERNAL bool _pool_is_pow2_or_zero(isize val)
{
    uint64_t uval = (uint64_t) val;
    return (uval & (uval-1)) == 0;
}

INTERNAL void* _pool_align_forward(void* ptr, isize align_to)
{
    ASSERT(_pool_is_pow2_or_zero(align_to) && align_to > 0);
    isize ptr_num = (isize) ptr;
    ptr_num += (-ptr_num) & (align_to - 1);
    return (void*) ptr_num;
}

INTERNAL int32_t _pool_alloc_get_bin_floor(uint32_t size)
{
    ASSERT(size >= 0);

    //Effectively computes floor(log_beta(size/M)) where 
    // beta is the logarithm base equal to sqrt(2) and M = POOL_ALLOC_MIN_SIZE_LOG2.
    // floor(log_beta(size)) = floor(log2(size/M)/log2(sqrt(2)) = 
    //                       = floor(log2(size/M)/0.5)
    //                       = floor(2*log2(size/M))
    //                       = floor(2*log2(size)) - 2*log2(M)
    //
    int32_t lower_bound_log2 = _pool_find_last_set_bit32(size);
    uint32_t lower_bound = (uint32_t) 1 << lower_bound_log2;
    uint32_t middle_point_offset = (uint32_t) 1 << (lower_bound_log2-1);

    int32_t res = 2*(int32_t)lower_bound_log2 + (int32_t) (size >= lower_bound + middle_point_offset);
    return res;
}

INTERNAL int32_t _pool_alloc_get_bin_ceil(uint32_t size)
{
    int32_t index = _pool_alloc_get_bin_floor(size);

    // Unless its power of two (thus 1 << lower_bound_log2 == size) we take the next bin!
    return index + (int32_t) !_pool_is_pow2_or_zero(size); 
}

INTERNAL isize _pool_alloc_ith_bin_size(int32_t bin_index)
{
    int32_t lower_bound_log2 = bin_index/2;
    isize main_size = (isize) 1 << lower_bound_log2;
    isize split_size = 0;
    if(bin_index % 2 == 1)
        split_size = (isize) 1 << (lower_bound_log2-1);

    isize size = (main_size + split_size)*POOL_ALLOC_MIN_SIZE;
    return size;
}

INTERNAL bool _pool_alloc_is_used(Pool_Allocator_Node* node)
{
    ASSERT((node->next_in_bin == 0) == (node->prev_in_bin == 0));
    return node->next_in_bin == 0;
}

INTERNAL void _pool_alloc_check_node_always(Pool_Allocator* allocator, uint32_t node_i, uint32_t flags, int32_t expected_bin)
{
    TEST(0 <= node_i && node_i < allocator->node_capacity);
    TEST(node_i != POOL_ALLOC_START && node_i != POOL_ALLOC_END, "Must not be START or END node!");
    Pool_Allocator_Node* node = &allocator->nodes[node_i];
    
    if(flags & POOL_ALLOC_CHECK_FREELIST)
    {
        #ifdef POOL_ALLOC_DEBUG
            TEST(node->offset == POOL_ALLOC_INVALID);
            TEST(node->prev == POOL_ALLOC_INVALID);
            TEST(node->next_in_bin == POOL_ALLOC_INVALID);
            TEST(node->prev_in_bin == POOL_ALLOC_INVALID);
        #endif // POOL_ALLOC_DEBUG
    }
    else
    {
        bool node_is_used = _pool_alloc_is_used(node);
        if(flags & POOL_ALLOC_CHECK_USED)
            TEST(node_is_used == true);
        if(flags & POOL_ALLOC_CHECK_UNUSED)
            TEST(node_is_used == false);
        if(flags & POOL_ALLOC_CHECK_BIN)
        {
            int32_t bin_i = _pool_alloc_get_bin_floor(node->size);
            TEST(bin_i == expected_bin);
        }

        TEST(node->offset + node->size <= allocator->memory_size);
        TEST(0 < node->size);
        TEST(node->next < allocator->node_capacity);
        TEST(node->prev < allocator->node_capacity);
        TEST(node->next_in_bin < allocator->node_capacity);
        TEST(node->prev_in_bin < allocator->node_capacity);

        if(flags & POOL_ALLOC_CHECK_DETAILED)
        {
            Pool_Allocator_Node* next = &allocator->nodes[node->next];
            Pool_Allocator_Node* prev = &allocator->nodes[node->prev];
            
            if(node->prev == POOL_ALLOC_START)
                TEST(prev->offset == node->offset);
            else
                TEST(prev->offset < node->offset);
            TEST(node->offset < next->offset);

            TEST(next->prev == node_i);
            TEST(prev->next == node_i);

            uint32_t calc_size = next->offset - node->offset;
            TEST(node->size == calc_size);

            if(node_is_used == false)
            {
                Pool_Allocator_Node* next_in_bin = &allocator->nodes[node->next_in_bin];
                Pool_Allocator_Node* prev_in_bin = &allocator->nodes[node->prev_in_bin];
                
                TEST(next_in_bin->prev_in_bin == node_i);
                TEST(prev_in_bin->next_in_bin == node_i);
            
                //If node is the only node in circular list its self referential from both sides
                TEST((node->next_in_bin == node_i) == (node->prev_in_bin == node_i));
            }
        }
    }
}

EXPORT void pool_alloc_check_invariants_always(Pool_Allocator* allocator, uint32_t flags)
{
    //Check fields
    TEST(allocator->nodes != NULL);
    TEST(2 + allocator->node_count <= allocator->node_capacity);
    TEST(allocator->allocation_count <= allocator->max_allocation_count);
    TEST(allocator->bytes_allocated <= allocator->max_bytes_allocated);

    //Check if bin free lists match the mask
    for(int32_t i = 0; i < POOL_ALLOC_BINS; i++)
    {
        bool has_ith_bin = allocator->bin_first_free[i] != 0;
        uint64_t ith_bit = (uint64_t) 1 << i;
        TEST(!!(allocator->bin_mask & ith_bit) == has_ith_bin);
    }
    
    //Check START and END nodes.
    Pool_Allocator_Node* start = &allocator->nodes[POOL_ALLOC_START];
    TEST(start->prev == POOL_ALLOC_INVALID);
    TEST(start->next_in_bin == 0);
    TEST(start->prev_in_bin == 0);
    TEST(start->offset == 0);
    TEST(start->size == 0);
    
    Pool_Allocator_Node* end = &allocator->nodes[POOL_ALLOC_END];
    TEST(end->next == POOL_ALLOC_INVALID);
    TEST(end->next_in_bin == 0);
    TEST(end->prev_in_bin == 0);
    TEST(end->offset == (uint32_t) allocator->memory_size);
    TEST(end->size == 0);
        
    if(flags & POOL_ALLOC_CHECK_ALL_NODES)
    {
        //Check free list //TODO
        uint32_t nodes_in_free_list = 0;
        for(uint32_t node_i = allocator->node_first_free; node_i != POOL_ALLOC_INVALID; nodes_in_free_list++)
        {
            _pool_alloc_check_node_always(allocator, node_i, POOL_ALLOC_CHECK_FREELIST | flags, 0);
            Pool_Allocator_Node* node = &allocator->nodes[node_i];
            node_i = node->next;
        }

        //Go through all nodes in all bins and check them.
        uint32_t nodes_in_bins = 0;
        for(int32_t bin_i = 0; bin_i < POOL_ALLOC_BINS; bin_i++)
        {
            uint32_t first_free = allocator->bin_first_free[bin_i];
            if(first_free == 0)
                continue;

            uint32_t in_bin_count = 0;
            for(uint32_t node_i = first_free;; )
            {
                in_bin_count++;
                TEST(in_bin_count < allocator->node_capacity);
                _pool_alloc_check_node_always(allocator, node_i, POOL_ALLOC_CHECK_UNUSED | POOL_ALLOC_CHECK_BIN | flags, bin_i);
                
                Pool_Allocator_Node* node = &allocator->nodes[node_i];
                node_i = node->next_in_bin;
                if(node_i == first_free)
                    break;
            }

            nodes_in_bins += in_bin_count;
        }

        //Go through all nodes in order
        uint32_t nodes_in_use = 0;
        uint32_t nodes_counted = 0;
        for(uint32_t node_i = POOL_ALLOC_START; node_i != POOL_ALLOC_INVALID; nodes_counted++)
        {
            TEST(nodes_counted < allocator->node_capacity);

            if(node_i != POOL_ALLOC_START && node_i != POOL_ALLOC_END)
                _pool_alloc_check_node_always(allocator, node_i, flags, 0);

            Pool_Allocator_Node* node = &allocator->nodes[node_i];
            nodes_in_use += (uint32_t) _pool_alloc_is_used(node);
            node_i = node->next;
        }
        
        TEST(allocator->node_count + 2 == nodes_counted);
        TEST(allocator->allocation_count + 2 == nodes_in_use);
        TEST(allocator->node_capacity == nodes_in_use + nodes_in_bins + nodes_in_free_list);
        int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
    }
}

INTERNAL void _pool_alloc_check_node(Pool_Allocator* allocator, uint32_t node_i, uint32_t flags)
{
    (void) allocator;
    (void) node_i;
    (void) flags;
    #ifdef POOL_ALLOC_DEBUG
        #ifdef POOL_ALLOC_DEBUG_SLOW
            flags |= POOL_ALLOC_CHECK_DETAILED;
        #else
            flags &= ~POOL_ALLOC_CHECK_DETAILED;
        #endif

        _pool_alloc_check_node_always(allocator, node_i, flags, 0);
    #endif
}

INTERNAL void _pool_alloc_check_invariants(Pool_Allocator* allocator)
{
    (void) allocator;
    #ifdef POOL_ALLOC_DEBUG
        uint32_t flags = 0;
        #ifdef POOL_ALLOC_DEBUG_SLOW
            flags |= POOL_ALLOC_CHECK_DETAILED;
        #endif
        
        #ifdef POOL_ALLOC_DEBUG_SLOW_SLOW
            flags |= POOL_ALLOC_CHECK_ALL_NODES;
        #endif

        pool_alloc_check_invariants_always(allocator, flags);
    #endif
}

INTERNAL void _pool_alloc_unlink_node_in_bin(Pool_Allocator* allocator, uint32_t node_i, int32_t bin_i)
{
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_UNUSED);
    Pool_Allocator_Node* node = &allocator->nodes[node_i];
    
    uint32_t* first_free = &allocator->bin_first_free[bin_i];
    //If is the only node bin
    if(node_i == node->prev_in_bin)
    {
        ASSERT(*first_free == node_i);

        *first_free = 0;
        allocator->bin_mask &= ~((uint64_t) 1 << bin_i);
    }
    else
    {
        Pool_Allocator_Node* next_in_bin = &allocator->nodes[node->next_in_bin];
        Pool_Allocator_Node* prev_in_bin = &allocator->nodes[node->prev_in_bin];
    
        next_in_bin->prev_in_bin = node->prev_in_bin;
        prev_in_bin->next_in_bin = node->next_in_bin;

        *first_free = node->next_in_bin;
    }
    
    node->next_in_bin = 0;
    node->prev_in_bin = 0; 
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_USED);
}

INTERNAL void _pool_alloc_link_node_in_bin(Pool_Allocator* allocator, uint32_t node_i, int32_t bin_i)
{
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_USED);
    Pool_Allocator_Node* node = &allocator->nodes[node_i];
    uint32_t* first_free = &allocator->bin_first_free[bin_i];
    node->next_in_bin = node_i;
    node->prev_in_bin = node_i;

    if(*first_free)
    {
        uint32_t bin_first_i = *first_free;
        Pool_Allocator_Node* bin_first = &allocator->nodes[bin_first_i];
        uint32_t bin_last_i = bin_first->prev_in_bin;
        Pool_Allocator_Node* bin_last = &allocator->nodes[bin_last_i];
            
        #ifdef POOL_ALLOC_DEBUG_SLOW
            _pool_alloc_check_node(allocator, bin_first_i, POOL_ALLOC_CHECK_UNUSED);
            _pool_alloc_check_node(allocator, bin_last_i, POOL_ALLOC_CHECK_UNUSED);
        #endif

        bin_first->prev_in_bin = node_i;
        bin_last->next_in_bin = node_i;

        node->next_in_bin = bin_first_i;
        node->prev_in_bin = bin_last_i;
            
        #ifdef POOL_ALLOC_DEBUG_SLOW
            _pool_alloc_check_node(allocator, bin_first_i, POOL_ALLOC_CHECK_UNUSED);
            _pool_alloc_check_node(allocator, bin_last_i, POOL_ALLOC_CHECK_UNUSED);
        #endif
    }
    
    *first_free = node_i;
    allocator->bin_mask |= (uint64_t) 1 << bin_i; 
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_UNUSED);
}

EXPORT uint32_t pool_alloc_allocate(Pool_Allocator* allocator, isize size, isize align)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(_pool_is_pow2_or_zero(align) && align > 0);

    _pool_alloc_check_invariants(allocator);
    if(size == 0)
        return 0;

    uint32_t adjusted_size = (uint32_t) size;
    uint32_t adjusted_align = POOL_ALLOC_MIN_SIZE;
    if(align > POOL_ALLOC_MIN_SIZE)
    {
        adjusted_align = align < POOL_ALLOC_MAX_ALIGN ? (uint32_t) align : POOL_ALLOC_MAX_ALIGN;
        adjusted_size += adjusted_align;
    }
    
    int32_t bin_from = _pool_alloc_get_bin_ceil(adjusted_size);
    uint64_t bins_mask = ((uint64_t) 1 << bin_from) - 1;
    uint64_t suitable_bin_mask = allocator->bin_mask & ~bins_mask;
    if(suitable_bin_mask == 0)
        return 0;

    int32_t bin_i = _pool_find_first_set_bit64(suitable_bin_mask);
    uint32_t node_i = allocator->bin_first_free[bin_i];
    Pool_Allocator_Node* __restrict node = &allocator->nodes[node_i]; 
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_UNUSED);
    
    //Update the first free of this bin
    _pool_alloc_unlink_node_in_bin(allocator, node_i, bin_i);

    ASSERT(node->size >= adjusted_size);
    uint32_t rem_size = node->size - adjusted_size;

    if(rem_size >= POOL_ALLOC_MIN_SIZE)
    {
        int32_t added_to_bin_i = _pool_alloc_get_bin_floor(rem_size);
        uint32_t next_i = node->next;
        uint32_t added_i = allocator->node_first_free;

        if(added_i != POOL_ALLOC_INVALID)
        {
            #ifdef POOL_ALLOC_DEBUG_SLOW
                _pool_alloc_check_node(allocator, added_i, POOL_ALLOC_CHECK_FREELIST);
                if(next_i != POOL_ALLOC_START && next_i != POOL_ALLOC_END)
                    _pool_alloc_check_node(allocator, next_i, 0);
            #endif

            ASSERT(node_i != next_i && next_i != added_i);
            Pool_Allocator_Node* __restrict next = &allocator->nodes[next_i];
            Pool_Allocator_Node* __restrict added = &allocator->nodes[added_i];
            allocator->node_first_free = added->next;

            //Link `added` between `node` and `next`
            added->offset = node->offset + adjusted_size;
            added->next = next_i;
            added->prev = node_i;
            added->size = rem_size;
            added->next_in_bin = 0;
            added->prev_in_bin = 0;
        
            node->size = adjusted_size;
            node->next = added_i;
            next->prev = added_i;
        
            allocator->node_count += 1;
            _pool_alloc_link_node_in_bin(allocator, added_i, added_to_bin_i);

            #ifdef POOL_ALLOC_DEBUG_SLOW
                if(next_i != POOL_ALLOC_START && next_i != POOL_ALLOC_END)
                    _pool_alloc_check_node(allocator, next_i, 0);
                _pool_alloc_check_node(allocator, added_i, POOL_ALLOC_CHECK_UNUSED);
            #endif
        }
    }

    allocator->allocation_count += 1;
    if(allocator->max_allocation_count < allocator->allocation_count)
        allocator->max_allocation_count = allocator->allocation_count;

    allocator->bytes_allocated += node->size*POOL_ALLOC_MIN_SIZE;
    if(allocator->max_bytes_allocated < allocator->bytes_allocated)
        allocator->max_bytes_allocated = allocator->bytes_allocated;

    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_USED);
    _pool_alloc_check_invariants(allocator);
    return node_i;
}

EXPORT void pool_alloc_deallocate(Pool_Allocator* allocator, uint32_t node_i)
{
    ASSERT(allocator);
    _pool_alloc_check_invariants(allocator);
    
    if(node_i == 0)
        return;
        
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_USED);
    Pool_Allocator_Node* __restrict node = &allocator->nodes[node_i]; 

    uint32_t original_size = node->size;
    uint32_t next_i = node->next;
    uint32_t prev_i = node->prev;
    ASSERT(next_i < allocator->node_capacity);
    ASSERT(prev_i < allocator->node_capacity);
    
    Pool_Allocator_Node* __restrict next = &allocator->nodes[next_i];
    Pool_Allocator_Node* __restrict prev = &allocator->nodes[prev_i];
    
    if(_pool_alloc_is_used(prev) == false)
    {
        _pool_alloc_check_node(allocator, prev_i, POOL_ALLOC_CHECK_UNUSED);

        Pool_Allocator_Node* __restrict prev_prev = &allocator->nodes[prev->prev];
        int32_t prev_bin_i = _pool_alloc_get_bin_floor(prev->size); 
        _pool_alloc_unlink_node_in_bin(allocator, prev_i, prev_bin_i);
        allocator->node_count -= 1;

        node->prev = prev->prev;
        prev_prev->next = node_i;
        node->size += prev->size;
        node->offset = prev->offset;

        prev->next = allocator->node_first_free;
        allocator->node_first_free = prev_i;
        
        #ifdef POOL_ALLOC_DEBUG
            prev->prev = POOL_ALLOC_INVALID;
            prev->next_in_bin = POOL_ALLOC_INVALID;
            prev->prev_in_bin = POOL_ALLOC_INVALID;
            prev->size = POOL_ALLOC_INVALID;
            prev->offset = POOL_ALLOC_INVALID;
        #endif
    }
    
    if(_pool_alloc_is_used(next) == false)
    {
        _pool_alloc_check_node(allocator, next_i, POOL_ALLOC_CHECK_UNUSED);

        Pool_Allocator_Node* __restrict next_next = &allocator->nodes[next->next];
        int32_t next_bin_i = _pool_alloc_get_bin_floor(next->size); 
        _pool_alloc_unlink_node_in_bin(allocator, next_i, next_bin_i);
        allocator->node_count -= 1;

        node->next = next->next;
        next_next->prev = node_i;
        node->size += next->size;
        
        next->next = allocator->node_first_free;
        allocator->node_first_free = next_i;

        #ifdef POOL_ALLOC_DEBUG
            next->prev = POOL_ALLOC_INVALID;
            next->next_in_bin = POOL_ALLOC_INVALID;
            next->prev_in_bin = POOL_ALLOC_INVALID;
            next->size = POOL_ALLOC_INVALID;
            next->offset = POOL_ALLOC_INVALID;
        #endif
    }

    int32_t bin_i = _pool_alloc_get_bin_floor(node->size); 
    _pool_alloc_link_node_in_bin(allocator, node_i, bin_i);
    
    ASSERT(allocator->allocation_count > 0);
    ASSERT(allocator->bytes_allocated > original_size);
    allocator->allocation_count -= 1;
    allocator->bytes_allocated -= original_size;
    
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_UNUSED);
    _pool_alloc_check_invariants(allocator);
}

EXPORT void pool_alloc_init(Pool_Allocator* allocator, void* memory, isize memory_size, isize node_capacity)
{
    ASSERT(allocator);
    ASSERT(memory_size >= 0);
    memset(allocator, 0, sizeof *allocator);   

    //Size for START and END nodes
    node_capacity += 2;

    allocator->nodes = (Pool_Allocator_Node*) malloc(node_capacity*sizeof(Pool_Allocator_Node));
    if(allocator->nodes == NULL)
        return;

    allocator->memory = (uint8_t*) memory;
    allocator->memory_size = (uint32_t) memory_size;
    allocator->node_capacity = (uint32_t)node_capacity;
    allocator->node_count = 0;

    #ifdef POOL_ALLOC_DEBUG_SLOW
    memset(allocator->nodes, -1, (size_t) node_capacity*sizeof(Pool_Allocator_Node));
    if(memory)
        memset(memory, -1, (size_t) memory_size);
    #endif

    allocator->node_first_free = POOL_ALLOC_INVALID;
    for(uint32_t i = allocator->node_capacity; i-- > 0;)
    {
        Pool_Allocator_Node* node = &allocator->nodes[i]; 
        node->next = allocator->node_first_free;
        allocator->node_first_free = i;

        node->prev = POOL_ALLOC_INVALID;
        node->next_in_bin = POOL_ALLOC_INVALID;
        node->prev_in_bin = POOL_ALLOC_INVALID;
        node->size = POOL_ALLOC_INVALID;
        node->offset = POOL_ALLOC_INVALID;
    }

    //Push START and END nodes
    uint32_t start_i = allocator->node_first_free;
    Pool_Allocator_Node* start = &allocator->nodes[start_i]; 
    allocator->node_first_free = start->next;
    
    uint32_t end_i = allocator->node_first_free;
    Pool_Allocator_Node* end = &allocator->nodes[end_i]; 
    allocator->node_first_free = end->next;
    
    //Push first node
    uint32_t first_i = allocator->node_first_free;
    Pool_Allocator_Node* first = &allocator->nodes[first_i]; 
    allocator->node_first_free = first->next;

    ASSERT(start_i == POOL_ALLOC_START);
    ASSERT(end_i == POOL_ALLOC_END);

    start->prev = POOL_ALLOC_INVALID;
    start->next = first_i;
    start->next_in_bin = 0; //mark used
    start->prev_in_bin = 0;
    start->offset = 0;
    start->size = 0;
    
    end->prev = first_i;
    end->next = POOL_ALLOC_INVALID;
    end->next_in_bin = 0; //mark used
    end->prev_in_bin = 0;
    end->offset = (uint32_t) memory_size;
    end->size = 0;

    first->prev = POOL_ALLOC_START;
    first->next = POOL_ALLOC_END;
    first->next_in_bin = 0;
    first->prev_in_bin = 0;
    first->size = (uint32_t) memory_size;
    first->offset = 0;

    int32_t first_bin_i = _pool_alloc_get_bin_floor(first->size);
    _pool_alloc_link_node_in_bin(allocator, first_i, first_bin_i);
    allocator->node_count += 1;
    
    _pool_alloc_check_invariants(allocator);
}

EXPORT void pool_alloc_reset(Pool_Allocator* allocator)
{
    //TODO
    pool_alloc_init(allocator, allocator->memory, allocator->memory_size, 0);
}

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_POOL_ALLOCATOR_TEST)) && !defined(JOT_POOL_ALLOCATOR_HAS_TEST)
#define JOT_POOL_ALLOCATOR_HAS_TEST

#ifndef STATIC_ARRAY_SIZE
    #define STATIC_ARRAY_SIZE(arr) (isize)(sizeof(arr)/sizeof((arr)[0]))
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

void test_pool_alloc_unit()
{
    isize memory_size = 50*1024;

    Pool_Allocator allocator = {0};
    pool_alloc_init(&allocator, NULL, memory_size, 1024);

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

    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
    {
        pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);
        allocs[i].node = pool_alloc_allocate(&allocator, allocs[i].size, allocs[i].align);
        pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);
    }
        
    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
    {
        pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);
        pool_alloc_deallocate(&allocator, allocs[i].node);
        pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);
    }
}

//tests wheter the data is equal to the specified bit pattern
bool memtest(const void* data, int val, isize size)
{
    uint8_t* udata = (uint8_t*) data;
    uint8_t uval = (uint8_t)val;
    for(isize i = 0; i < size; i++)
        if(udata[i] != uval)
            return false;

    return true;
}

INTERNAL double _pool_clock_s()
{
    return (double) clock() / CLOCKS_PER_SEC;
}

INTERNAL isize _pool_random_range(isize from, isize to)
{
    if(from == to)
        return from;

    return rand()%(to - from) + from;
}

INTERNAL double _pool_random_interval(double from, double to)
{
    double random = (double) rand() / RAND_MAX;
    return (to - from)*random + from;
}

void test_pool_alloc_stress(double seconds, isize at_once)
{
    enum {
        MAX_SIZE_LOG2 = 17, //1/8 MB = 256 KB
        MAX_ALIGN_LOG2 = 5,
        MAX_AT_ONCE = 1024,
    };
    const double MAX_PERTURBATION = 0.2;
    
    ASSERT(at_once < MAX_AT_ONCE);
    isize memory_size = 1024*1024*1024;

    Pool_Allocator allocator = {0};
    pool_alloc_init(&allocator, NULL, memory_size, MAX_AT_ONCE);
    
    struct {
        uint32_t size;
        uint32_t align;
        uint32_t node;
    } allocs[MAX_AT_ONCE] = {0};

    isize iter = 0;
    for(double start = _pool_clock_s(); _pool_clock_s() - start < seconds;)
    {
        isize i = _pool_random_range(0, at_once);
        if(iter < at_once)
            i = iter;
        else
        {
            pool_alloc_deallocate(&allocator, allocs[i].node);
            pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);
        }
        
        double perturbation = 1 + _pool_random_interval(-MAX_PERTURBATION, MAX_PERTURBATION);
        isize random_align_shift = _pool_random_range(0, MAX_ALIGN_LOG2);
        isize random_size_shift = _pool_random_range(0, MAX_SIZE_LOG2);

        //Random exponentially distributed sizes with small perturbances.
        allocs[i].size = (int32_t)(((isize) 1 << random_size_shift) * perturbation);
        allocs[i].align = (int32_t) ((isize) 1 << random_align_shift);
        allocs[i].node = pool_alloc_allocate(&allocator, allocs[i].size, allocs[i].align);
        
        pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);

        iter += 1;
    }
}

void test_pool_alloc(double seconds)
{
    printf("[TEST]: Pool allocator sizes below:\n");
    for(isize i = 0; i < POOL_ALLOC_BINS; i++)
        printf("[TEST]: %2lli -> %lli\n", i, _pool_alloc_ith_bin_size((int32_t) i));

    test_pool_alloc_unit();
    test_pool_alloc_stress(seconds/4, 1);
    test_pool_alloc_stress(seconds/4, 10);
    test_pool_alloc_stress(seconds/4, 100);
    test_pool_alloc_stress(seconds/4, 200);

    printf("[TEST]: test_pool_alloc(%lf) success!\n", seconds);
}

//Include the benchmark only when being included alongside the rest of the codebase
// since I cant be bothered to make it work without any additional includes
#ifdef JOT_ALLOCATOR
    #include "perf.h"
    #include "random.h"
    #include "log.h"
    void benchmark_pool_alloc_single(double seconds, isize at_once, isize min_size, isize max_size, isize min_align_log2, isize max_align_log2)
    {
        LOG_INFO("BENCH", "Running benchmarks for %s with at_once:%lli size:[%lli, %lli) align_log:[%lli %lli)", 
            format_seconds(seconds).data, at_once, min_size, max_size, min_align_log2, max_align_log2);

        enum {
            CACHED_COUNT = 1024,
            BATCH_SIZE = 1,
        };
        typedef struct Alloc {
            union {
                void* ptr;
                uint32_t node;
            };
        } Alloc;

        typedef struct Cached_Random {
            int32_t size;
            int32_t align;
            int32_t index;
        } Cached_Random;

        enum {
            DO_ARENA,
            DO_POOL,
            DO_MALLOC,
        };
    
        Arena arena = {0};
        TEST(arena_init(&arena, 0, 0));
        isize memory_size = 250*1024*1024;
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

        Pool_Allocator pool = {0};
        pool_alloc_init(&pool, NULL, memory_size, at_once);

        Perf_Stats stats_pool_alloc = {0};
        Perf_Stats stats_pool_free = {0};
    
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
            
            if(j == DO_POOL) {
                stats_alloc = &stats_pool_alloc;
                stats_free = &stats_pool_free;
            }
            
            if(j == DO_MALLOC) {
                stats_alloc = &stats_malloc_alloc;
                stats_free = &stats_malloc_free;
            }

            isize curr_batch = 0;
            isize accumulated_alloc = 0;
            isize accumulated_free = 0;

            isize active_allocs = 0;
            for(Perf_Benchmark bench_alloc = {0}, bench_free = {0}; ;) 
            {
                bool continue1 = perf_benchmark_custom(&bench_alloc, stats_alloc, warmup, seconds, BATCH_SIZE);;
                bool continue2 = perf_benchmark_custom(&bench_free, stats_free, warmup, seconds, BATCH_SIZE);;
                if(continue1 == false || continue2 == false)
                    break;
                    
                _pool_alloc_check_invariants(&pool);

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
                    if(j == DO_POOL) 
                        pool_alloc_deallocate(&pool, allocs[i].node);
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
                if(j == DO_POOL) 
                    allocs[i].node = pool_alloc_allocate(&pool, random.size, random.align);
                if(j == DO_ARENA) 
                    allocs[i].ptr = arena_push_nonzero(&arena, random.size, random.align);
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
        }
    
        free(allocs);
        free(randoms);
        arena_deinit(&arena);

        log_perf_stats_hdr("BENCH", LOG_INFO, "ALLOC:        ");
        log_perf_stats_row("BENCH", LOG_INFO, "arena         ", stats_arena_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf          ", stats_pool_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc        ", stats_malloc_alloc);
        
        log_perf_stats_hdr("BENCH", LOG_INFO, "FREE:         ");
        log_perf_stats_row("BENCH", LOG_INFO, "arena         ", stats_arena_free);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf          ", stats_pool_free);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc        ", stats_malloc_free);
    }

    void benchmark_pool_alloc(double seconds)
    {
        benchmark_pool_alloc_single(seconds, 4096, 8, 64, 0, 4);
        benchmark_pool_alloc_single(seconds, 1024, 64, 512, 0, 4);
        benchmark_pool_alloc_single(seconds, 1024, 8, 64, 0, 4);
        benchmark_pool_alloc_single(seconds, 256, 64, 512, 0, 4);
        benchmark_pool_alloc_single(seconds, 1024, 4000, 8000, 0, 4);
    }
    #endif
#endif