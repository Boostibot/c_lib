#ifndef JOT_TLSF_ALLOCATOR
#define JOT_TLSF_ALLOCATOR

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
// more control. The whole tlsf can be trivially deallocated at once 
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
// - All numbers which are actually scalled by MIN_SIZE are labeled [thing]_div_min. The conversion 
//   to and from _div_min is super fast as MIN_SIZE is compile time known power of 2. Even if it werent
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
//   _tlsf_check_[thing]_always() which are kept around always and two wrappers _tlsf_check_[thing]()
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

#define TLSF_ALLOC_MIN_SIZE             8
#define TLSF_ALLOC_MIN_SIZE_LOG2        3
#define TLSF_ALLOC_MAX_SIZE             ((uint64_t) UINT32_MAX * TLSF_ALLOC_MIN_SIZE)
#define TLSF_ALLOC_BINS                 64
#define TLSF_ALLOC_MAX_ALIGN            4096 //One page 
#define TLSF_ALLOC_PACKED_BIN_BITS      9
#define TLSF_ALLOC_PACKED_BIN_OFFSET    21
#define TLSF_ALLOC_IS_USED_BIT          ((uint32_t) 1 << 31)
#define TLSF_ALLOC_IS_MARKED_BIT        ((uint32_t) 1 << 30)

#define TLSF_ALLOC_CHECK_UNUSED     ((uint32_t) 1 << 0)
#define TLSF_ALLOC_CHECK_USED       ((uint32_t) 1 << 1)
#define TLSF_ALLOC_CHECK_DETAILED   ((uint32_t) 1 << 2)
#define TLSF_ALLOC_CHECK_ALL_NODES  ((uint32_t) 1 << 3)
#define TLSF_ALLOC_CHECK_BIN        ((uint32_t) 1 << 4)

#ifndef NDEBUG
    //#define TLSF_ALLOC_DEBUG            //Enables basic safery checks on passed in nodes. Helps to find overwrites
    //#define TLSF_ALLOC_DEBUG_SLOW       //Enebles extensive checks on nodes. Also fills data to garavge on alloc/free.
    //#define TLSF_ALLOC_DEBUG_SLOW_SLOW  //Checks all nodes on every entry and before return of every function. Is extremely slow and should only be used when testing this allocator
#endif

typedef struct Tlsf_Allocator_Bin_Info {
    uint32_t first_free_div_min;
    //uint32_t num_free;
    //uint32_t total;
} Tlsf_Allocator_Bin_Info;

typedef struct Tlsf_Allocator {
    //i-th bit indicates wheter theres at least single space in i-th bin
    //0-th bin has size TLSF_ALLOC_MIN_SIZE
    //63-th bin has size TLSF_ALLOC_MAX_SIZE
    uint64_t non_filled_bins;
    uint8_t* memory;
    isize memory_size;
    uint32_t first_node_div_min;
    uint32_t _pading;
    Tlsf_Allocator_Bin_Info bin_info[TLSF_ALLOC_BINS];
    
    isize max_bytes_allocated;
    isize bytes_allocated;
    isize num_nodes;
} Tlsf_Allocator;

EXPORT void tlsf_alloc_init(Tlsf_Allocator* allocator, void* memory, isize memory_size);
EXPORT void* tlsf_alloc_allocate(Tlsf_Allocator* allocator, isize size, isize align);
EXPORT void tlsf_alloc_deallocate(Tlsf_Allocator* allocator, void* ptr, isize size, isize align);
EXPORT void tlsf_alloc_free(Tlsf_Allocator* allocator, void* ptr);
EXPORT void tlsf_alloc_free_all(Tlsf_Allocator* allocator);

//Checks wheter the allocator is in valid state. If is not aborts.
// Flags can be TLSF_ALLOC_CHECK_DETAILED and TLSF_ALLOC_CHECK_ALL_NODES.
EXPORT void tlsf_alloc_check_invariants_always(Tlsf_Allocator* allocator, uint32_t flags);

#endif

//=========================  IMPLEMENTATION BELOW ==================================================
#if (defined(JOT_ALL_IMPL) || defined(JOT_TLSF_ALLOCATOR_IMPL)) && !defined(JOT_TLSF_ALLOCATOR_HAS_IMPL)
#define JOT_TLSF_ALLOCATOR_IMPL

typedef struct Tlsf_Allocator_Node {
    //xxx_div_min means xxx divided by TLSF_ALLOC_MIN_SIZE. See usage code to.
    uint32_t next_div_min; 
    uint32_t prev_div_min;

    uint32_t next_in_bin_div_min; 
    uint32_t prev_in_bin_div_min; 

    uint32_t size_div_min;

    //This field contains all the other needed info.
    //The bit sections are as folows:
    //0 ..20 - alignment 
    //21..29 - bin index
    //30     - marker flag (used only when TLSF_ALLOC_CHECK_ALL_NODES)
    //31     - is used flag
    //
    //Note that for both alignment and bin index we reserve much more
    // bits then necessary. This is so that we can identify errors.
    // (we could just as enforce the spare bitsh to be 0 but that is harder to 
    // interpret).
    uint32_t packed;

    //next = (uint8_t*) this_node + this_node->next_div_min*TLSF_ALLOC_MIN_SIZE
    //prev = (uint8_t*) this_node + this_node->prev_div_min*TLSF_ALLOC_MIN_SIZE
} Tlsf_Allocator_Node;

typedef struct Tlsf_Allocator_Unpacked {
    uint32_t align_skip;
    int32_t bin_index;
    uint32_t flags;
} Tlsf_Allocator_Unpacked;


#if defined(_MSC_VER)
#include <intrin.h>
    inline static int32_t _tlsf_find_last_set_bit32(uint32_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanReverse(&out, (unsigned long) num);
        return (int32_t) out;
    }
    
    inline static int32_t _tlsf_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
#elif defined(__GNUC__) || defined(__clang__)
    inline static int32_t _tlsf_find_last_set_bit32(uint32_t num)
    {
        ASSERT(num != 0);
        return 32 - __builtin_ctz((unsigned int) num) - 1;
    }
    
    inline static int32_t _tlsf_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        return __builtin_ffsll((long long) num) - 1;
    }
#else
    #error unsupported compiler!
#endif

INTERNAL bool _tlsf_is_pow2_or_zero(isize val)
{
    uint64_t uval = (uint64_t) val;
    return (uval & (uval-1)) == 0;
}

INTERNAL void* _tlsf_align_forward(void* ptr, isize align_to)
{
    ASSERT(_tlsf_is_pow2_or_zero(align_to) && align_to > 0);
    isize ptr_num = (isize) ptr;
    ptr_num += (-ptr_num) & (align_to - 1);
    return (void*) ptr_num;
}

INTERNAL int32_t _tlsf_alloc_get_bin_floor(uint32_t size_div_min)
{
    ASSERT(size_div_min >= 0);

    //Effectively computes floor(log_beta(size/M)) where 
    // beta is the logarithm base equal to sqrt(2) and M = TLSF_ALLOC_MIN_SIZE_LOG2.
    // floor(log_beta(size)) = floor(log2(size/M)/log2(sqrt(2)) = 
    //                       = floor(log2(size/M)/0.5)
    //                       = floor(2*log2(size/M))
    //                       = floor(2*log2(size)) - 2*log2(M)
    //
    int32_t lower_bound_log2 = _tlsf_find_last_set_bit32(size_div_min);
    uint32_t lower_bound = (uint32_t) 1 << lower_bound_log2;
    uint32_t middle_point_offset = (uint32_t) 1 << (lower_bound_log2-1);

    int32_t res = 2*(int32_t)lower_bound_log2 + (int32_t) (size_div_min >= lower_bound + middle_point_offset);
    return res;
}

INTERNAL int32_t _tlsf_alloc_get_bin_ceil(uint32_t size_div_min)
{
    int32_t index = _tlsf_alloc_get_bin_floor(size_div_min);

    // Unless its power of two (thus 1 << lower_bound_log2 == size_div_min) we take the next bin!
    return index + (int32_t) !_tlsf_is_pow2_or_zero(size_div_min); 
}

INTERNAL isize _tlsf_alloc_ith_bin_size(int32_t bin_index)
{
    int32_t lower_bound_log2 = bin_index/2;
    isize main_size = (isize) 1 << lower_bound_log2;
    isize split_size = 0;
    if(bin_index % 2 == 1)
        split_size = (isize) 1 << (lower_bound_log2-1);

    isize size = (main_size + split_size)*TLSF_ALLOC_MIN_SIZE;
    return size;
}

INTERNAL uint32_t _tlsf_alloc_pack(Tlsf_Allocator_Unpacked unpacked)
{
    ASSERT(unpacked.align_skip <= TLSF_ALLOC_MAX_ALIGN);
    ASSERT(unpacked.bin_index < TLSF_ALLOC_BINS);

    return unpacked.align_skip | (uint32_t) unpacked.bin_index << TLSF_ALLOC_PACKED_BIN_OFFSET | unpacked.flags;
}

INTERNAL Tlsf_Allocator_Unpacked _tlsf_alloc_unpack(uint32_t packed)
{
    uint32_t align_skip_mask = (1 << TLSF_ALLOC_PACKED_BIN_OFFSET) - 1;
    uint32_t bin_index_mask = (1 << TLSF_ALLOC_PACKED_BIN_BITS) - 1;

    Tlsf_Allocator_Unpacked unpacked = {0};
    unpacked.flags = packed;
    unpacked.align_skip = packed & align_skip_mask;
    unpacked.bin_index = (packed >> TLSF_ALLOC_PACKED_BIN_OFFSET) & bin_index_mask;

    return unpacked;
}

INTERNAL Tlsf_Allocator_Node* _tlsf_alloc_get_node(Tlsf_Allocator* allocator, uint32_t node_div_min)
{
    return (Tlsf_Allocator_Node*)(void*)(allocator->memory + node_div_min*TLSF_ALLOC_MIN_SIZE);
}
INTERNAL void _tlsf_alloc_check_node_always(Tlsf_Allocator* allocator, Tlsf_Allocator_Node* node_ptr, uint32_t flags, int32_t expected_bin)
{
    TEST(node_ptr);

    isize offset = (uint8_t*) node_ptr - (uint8_t*) allocator->memory;
    uint32_t node_div_min = (uint32_t) (offset/TLSF_ALLOC_MIN_SIZE);
    TEST(node_div_min*TLSF_ALLOC_MIN_SIZE == offset, "Needs to be multiple of TLSF_ALLOC_MIN_SIZE");

    Tlsf_Allocator_Node* node = _tlsf_alloc_get_node(allocator, node_div_min);
    Tlsf_Allocator_Unpacked unpacked = _tlsf_alloc_unpack(node->packed);

    TEST(unpacked.align_skip <= TLSF_ALLOC_MAX_ALIGN);
    bool node_is_used = !!(unpacked.flags & TLSF_ALLOC_IS_USED_BIT);
    if(flags & TLSF_ALLOC_CHECK_USED)
        TEST(node_is_used == true);
    if(flags & TLSF_ALLOC_CHECK_UNUSED)
        TEST(node_is_used == false);
    if(flags & TLSF_ALLOC_CHECK_BIN)
        TEST(unpacked.bin_index == expected_bin);

    TEST((isize) (node->size_div_min+node_div_min)*TLSF_ALLOC_MIN_SIZE < allocator->memory_size);
    TEST((isize) node->next_div_min*TLSF_ALLOC_MIN_SIZE < allocator->memory_size);
    TEST((isize) node->prev_div_min*TLSF_ALLOC_MIN_SIZE < allocator->memory_size);
    TEST((isize) node->next_in_bin_div_min*TLSF_ALLOC_MIN_SIZE < allocator->memory_size);
    TEST((isize) node->prev_in_bin_div_min*TLSF_ALLOC_MIN_SIZE < allocator->memory_size);

    if(flags & TLSF_ALLOC_CHECK_DETAILED)
    {
        int32_t bin = node->size_div_min > 0 ? _tlsf_alloc_get_bin_floor(node->size_div_min) : 0;
        TEST(bin == unpacked.bin_index);

        Tlsf_Allocator_Node* next = _tlsf_alloc_get_node(allocator, node->next_div_min);
        Tlsf_Allocator_Node* prev = _tlsf_alloc_get_node(allocator, node->prev_div_min);
        Tlsf_Allocator_Node* next_in_bin = _tlsf_alloc_get_node(allocator, node->next_in_bin_div_min);
        Tlsf_Allocator_Node* prev_in_bin = _tlsf_alloc_get_node(allocator, node->prev_in_bin_div_min);

        //If node is the only node in circular list its self referential from both sides
        TEST((node->next_div_min == node_div_min) == (node->prev_div_min == node_div_min));
        TEST((node->next_in_bin_div_min == node_div_min) == (node->prev_in_bin_div_min == node_div_min));

        //Proper connections between neighbours
        TEST(next->prev_div_min == node_div_min);
        TEST(prev->next_div_min == node_div_min);
        TEST(next_in_bin->prev_in_bin_div_min == node_div_min);
        TEST(prev_in_bin->next_in_bin_div_min == node_div_min);
    }
}

EXPORT void tlsf_alloc_check_invariants_always(Tlsf_Allocator* allocator, uint32_t flags)
{
    //Check if bin free lists match the mask
    uint64_t built_non_filled_bins = 0;
    for(int32_t i = 0; i < TLSF_ALLOC_BINS; i++)
    {
        uint64_t has_ith_bin = allocator->bin_info[i].first_free_div_min != 0;
        built_non_filled_bins |= has_ith_bin << i;
        uint64_t ith_bit = (uint64_t) 1 << i;
        TEST((allocator->non_filled_bins & ith_bit) == has_ith_bin << i);
    }
    TEST(allocator->non_filled_bins == built_non_filled_bins);

    //Check nil node
    _tlsf_alloc_check_node_always(allocator, (Tlsf_Allocator_Node*) allocator->memory, TLSF_ALLOC_CHECK_UNUSED, 0);
        
    if(flags & TLSF_ALLOC_CHECK_ALL_NODES)
    {
        //Go through all nodes in all bins and mark them.
        //They have to be marked exactly once, have to be free, and have to belong to the right bin.
        for(int32_t bin_i = 0; bin_i < TLSF_ALLOC_BINS; bin_i++)
        {
            uint32_t first_free = allocator->bin_info[bin_i].first_free_div_min;
            if(first_free == 0)
                continue;

            for(uint32_t node_div_min = first_free;;)
            {
                Tlsf_Allocator_Node* node = _tlsf_alloc_get_node(allocator, node_div_min);
                _tlsf_alloc_check_node_always(allocator, node, TLSF_ALLOC_CHECK_UNUSED | TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_BIN, bin_i);

                ASSERT(!(node->packed & TLSF_ALLOC_IS_MARKED_BIT));
                node->packed |= TLSF_ALLOC_IS_MARKED_BIT;

                node_div_min = node->next_in_bin_div_min;
                if(node_div_min == first_free)
                    break;
            }
        }

        //Go through all nodes. Node must be mark iff it is free (ie. all free nodes are reachable from some bin).
        int32_t counted_nodes = 0;
        for(uint32_t node_div_min = allocator->first_node_div_min;;)
        {
            counted_nodes += 1;

            Tlsf_Allocator_Node* node = _tlsf_alloc_get_node(allocator, node_div_min);
            _tlsf_alloc_check_node_always(allocator, node, TLSF_ALLOC_CHECK_DETAILED, 0);
            
            //Free <=> marked. Also clear marked.
            bool is_marked = !!(node->packed & TLSF_ALLOC_IS_MARKED_BIT);
            bool is_free = !(node->packed & TLSF_ALLOC_IS_USED_BIT);
            ASSERT(is_marked == is_free);
            node->packed &= ~TLSF_ALLOC_IS_MARKED_BIT;

            //If we are back at the start stop
            node_div_min = node->next_div_min;
            if(node_div_min == allocator->first_node_div_min)
                break;
        }

        TEST(allocator->num_nodes == counted_nodes);
    }
}

INTERNAL void _tlsf_alloc_check_node(Tlsf_Allocator* allocator, Tlsf_Allocator_Node* node_ptr, uint32_t flags)
{
    (void) allocator;
    (void) node_ptr;
    (void) flags;
    #ifdef TLSF_ALLOC_DEBUG
        #ifdef TLSF_ALLOC_DEBUG_SLOW
            flags |= TLSF_ALLOC_CHECK_DETAILED;
        #else
            flags &= ~TLSF_ALLOC_CHECK_DETAILED;
        #endif

        _tlsf_alloc_check_node_always(allocator, node_ptr, flags, 0);
    #endif
}

INTERNAL void _tlsf_alloc_check_invariants(Tlsf_Allocator* allocator)
{
    (void) allocator;
    #ifdef TLSF_ALLOC_DEBUG
        uint32_t flags = 0;
        #ifdef TLSF_ALLOC_DEBUG_SLOW
            flags |= TLSF_ALLOC_CHECK_DETAILED;
        #endif
        
        #ifdef TLSF_ALLOC_DEBUG_SLOW_SLOW
            flags |= TLSF_ALLOC_CHECK_ALL_NODES;
        #endif

        tlsf_alloc_check_invariants_always(allocator, flags);
    #endif
}

INTERNAL void _tlsf_alloc_unlink_node_in_bin(Tlsf_Allocator* allocator, Tlsf_Allocator_Node* node, uint32_t node_div_min, int32_t bin_i)
{
    ASSERT(!(node->packed & TLSF_ALLOC_IS_USED_BIT), "Does not make sense to unlink unused node!");
    
    uint32_t* first_free = &allocator->bin_info[bin_i].first_free_div_min;
    //If is the only node bin
    if(node_div_min == node->prev_in_bin_div_min)
    {
        ASSERT(*first_free == node_div_min);

        *first_free = 0;
        allocator->non_filled_bins &= ~((uint64_t) 1 << bin_i);
    }
    else
    {
        Tlsf_Allocator_Node* next_in_bin = _tlsf_alloc_get_node(allocator, node->next_in_bin_div_min);
        Tlsf_Allocator_Node* prev_in_bin = _tlsf_alloc_get_node(allocator, node->prev_in_bin_div_min);
    
        next_in_bin->prev_in_bin_div_min = node->prev_in_bin_div_min;
        prev_in_bin->next_in_bin_div_min = node->next_in_bin_div_min;

        *first_free = node->next_in_bin_div_min;
    }
    
    node->packed |= TLSF_ALLOC_IS_USED_BIT;
    node->next_in_bin_div_min = node_div_min;
    node->prev_in_bin_div_min = node_div_min; 
}

INTERNAL void _tlsf_alloc_link_node_in_bin(Tlsf_Allocator* allocator, Tlsf_Allocator_Node* node, uint32_t node_div_min, int32_t bin_i)
{
    Tlsf_Allocator_Bin_Info* bin = &allocator->bin_info[bin_i];
    node->next_in_bin_div_min = node_div_min;
    node->prev_in_bin_div_min = node_div_min;

    if(bin->first_free_div_min)
    {
        uint32_t bin_first_div_min = bin->first_free_div_min;
        Tlsf_Allocator_Node* bin_first = _tlsf_alloc_get_node(allocator, bin_first_div_min);
        uint32_t bin_last_div_min = bin_first->prev_in_bin_div_min;
        Tlsf_Allocator_Node* bin_last = _tlsf_alloc_get_node(allocator, bin_last_div_min);
            
        #ifdef TLSF_ALLOC_DEBUG_SLOW
            _tlsf_alloc_check_node(allocator, bin_first, TLSF_ALLOC_CHECK_UNUSED);
            _tlsf_alloc_check_node(allocator, bin_last, TLSF_ALLOC_CHECK_UNUSED);
        #endif

        bin_first->prev_in_bin_div_min = node_div_min;
        bin_last->next_in_bin_div_min = node_div_min;

        node->next_in_bin_div_min = bin_first_div_min;
        node->prev_in_bin_div_min = bin_last_div_min;
            
        #ifdef TLSF_ALLOC_DEBUG_SLOW
            _tlsf_alloc_check_node(allocator, bin_first, TLSF_ALLOC_CHECK_UNUSED);
            _tlsf_alloc_check_node(allocator, bin_last, TLSF_ALLOC_CHECK_UNUSED);
        #endif
    }
    
    node->packed &= ~TLSF_ALLOC_IS_USED_BIT;
    bin->first_free_div_min = node_div_min;
    allocator->non_filled_bins |= (uint64_t) 1 << bin_i; 
}

EXPORT void* tlsf_alloc_allocate(Tlsf_Allocator* allocator, isize size, isize align)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(_tlsf_is_pow2_or_zero(align) && align > 0);

    _tlsf_alloc_check_invariants(allocator);
    if(size == 0)
        return NULL;

    isize adjusted_size = size;
    isize adjusted_align = TLSF_ALLOC_MIN_SIZE;
    if(align > TLSF_ALLOC_MIN_SIZE)
    {
        adjusted_align = align < TLSF_ALLOC_MAX_ALIGN ? align : TLSF_ALLOC_MAX_ALIGN;
        adjusted_size = size + adjusted_align;
    }
    
    uint32_t size_div_min = (uint32_t) ((adjusted_size + TLSF_ALLOC_MIN_SIZE - 1) / TLSF_ALLOC_MIN_SIZE); //div round up.
    int32_t bin_from = _tlsf_alloc_get_bin_ceil(size_div_min);
    uint64_t bins_mask = ((uint64_t) 1 << bin_from) - 1;
    uint64_t suitable_non_filled_bins = allocator->non_filled_bins & ~bins_mask;
    if(suitable_non_filled_bins == 0)
        return NULL;

    int32_t bin_i = _tlsf_find_first_set_bit64(suitable_non_filled_bins);
    Tlsf_Allocator_Bin_Info* bin = &allocator->bin_info[bin_i];

    uint32_t node_div_min = bin->first_free_div_min;
    Tlsf_Allocator_Node* node = _tlsf_alloc_get_node(allocator, node_div_min); 
    _tlsf_alloc_check_node(allocator, node, TLSF_ALLOC_CHECK_UNUSED);
    
    //Update the first free of this bin
    _tlsf_alloc_unlink_node_in_bin(allocator, node, node_div_min, bin_i);

    ASSERT(node->size_div_min >= size_div_min);
    uint32_t rem_size_div_min = node->size_div_min - size_div_min;

    Tlsf_Allocator_Node* added = NULL;

    //If there is enough size left to split and we would be wasting too much memory split.
    // We place the second constraint as without it we would be splitting on almost every big allocation even if
    // it would nearly perfectly fit. @TODO: customizable?
    if(rem_size_div_min >= (TLSF_ALLOC_MIN_SIZE + sizeof(Tlsf_Allocator_Node))/TLSF_ALLOC_MIN_SIZE)
        //&& rem_size_div_min >= size_div_min*TLSF_ALLOC_MAX_WASTE_FRAC_NUM/TLSF_ALLOC_MAX_WASTE_FRAC_DEN)
    {
        _tlsf_alloc_check_invariants(allocator);
        uint32_t added_node_size = rem_size_div_min - sizeof(Tlsf_Allocator_Node)/TLSF_ALLOC_MIN_SIZE;
        int32_t added_to_bin_i = _tlsf_alloc_get_bin_floor(added_node_size);
        uint32_t next_div_min = node->next_div_min;
        uint32_t added_div_min = node_div_min + sizeof(Tlsf_Allocator_Node)/TLSF_ALLOC_MIN_SIZE + size_div_min;

        Tlsf_Allocator_Node* next = _tlsf_alloc_get_node(allocator, next_div_min);
        added = _tlsf_alloc_get_node(allocator, added_div_min);

        #ifdef TLSF_ALLOC_DEBUG_SLOW
            memset(added, -1, sizeof *added);
            ASSERT(added != node);

            _tlsf_alloc_check_node(allocator, next, 0);
        #endif
        
        Tlsf_Allocator_Unpacked unpacked = {0};
        unpacked.bin_index = added_to_bin_i;
        added->packed = _tlsf_alloc_pack(unpacked);

        //Link `added` between `node` and `next`
        added->next_div_min = next_div_min;
        added->prev_div_min = node_div_min;
        added->size_div_min = added_node_size;

        node->next_div_min = added_div_min;
        next->prev_div_min = added_div_min;

        //Update size and bin of the shunk node
        node->size_div_min = size_div_min;
        bin_i = bin_from - (int32_t) !_tlsf_is_pow2_or_zero(node->size_div_min);
        
        allocator->num_nodes += 1;
        ASSERT(added != node);
        _tlsf_alloc_link_node_in_bin(allocator, added, added_div_min, added_to_bin_i);

        #ifdef TLSF_ALLOC_DEBUG_SLOW
            if(node != next) //node does not have updated packed at this point.
                _tlsf_alloc_check_node(allocator, next, 0);
            _tlsf_alloc_check_node(allocator, added, TLSF_ALLOC_CHECK_UNUSED);
        #endif
    }
    
    #ifdef TLSF_ALLOC_DEBUG_SLOW
        memset(node + 1, -1, adjusted_size);
    #endif

    void* ptr = _tlsf_align_forward(node + 1, adjusted_align);
    isize align_skip = (isize) ((uint8_t*) ptr - (uint8_t*) node);

    Tlsf_Allocator_Unpacked unpacked = {0};
    unpacked.align_skip = (uint32_t) align_skip;
    unpacked.bin_index = bin_i;
    unpacked.flags = TLSF_ALLOC_IS_USED_BIT;
    
    uint32_t packed = _tlsf_alloc_pack(unpacked);
    *((uint32_t*) ptr - 1) = packed;
    node->packed = packed;
    
    allocator->bytes_allocated += node->size_div_min*TLSF_ALLOC_MIN_SIZE;
    if(allocator->max_bytes_allocated < allocator->bytes_allocated)
        allocator->max_bytes_allocated = allocator->bytes_allocated;

    _tlsf_alloc_check_node(allocator, node, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_invariants(allocator);
    return ptr;
}


EXPORT Tlsf_Allocator_Node* _tlsf_alloc_get_allocated_node(Tlsf_Allocator* allocator, void* ptr)
{
    Tlsf_Allocator_Unpacked read_unpacked = _tlsf_alloc_unpack(*((uint32_t*) ptr - 1));
    ASSERT(read_unpacked.align_skip <= TLSF_ALLOC_MAX_ALIGN && read_unpacked.bin_index < TLSF_ALLOC_BINS, "Bad packed! This is probably due to buffer underflow!");
    Tlsf_Allocator_Node* node = (Tlsf_Allocator_Node*)(void*)((uint8_t*) ptr - read_unpacked.align_skip);
    _tlsf_alloc_check_node(allocator, node, TLSF_ALLOC_CHECK_USED);

    return node;
}

EXPORT void tlsf_alloc_free(Tlsf_Allocator* allocator, void* ptr)
{
    ASSERT(allocator);
    _tlsf_alloc_check_invariants(allocator);
    
    if(ptr == NULL)
        return;

    Tlsf_Allocator_Node* node = _tlsf_alloc_get_allocated_node(allocator, ptr);
    _tlsf_alloc_check_node(allocator, node, TLSF_ALLOC_CHECK_USED);

    allocator->bytes_allocated -= node->size_div_min*TLSF_ALLOC_MIN_SIZE;
    ASSERT(allocator->bytes_allocated >= 0);
    
    uint32_t node_div_min = (uint32_t)((uint64_t) ((uint8_t*) node - allocator->memory)/TLSF_ALLOC_MIN_SIZE);
    uint32_t prev_div_min = node->prev_div_min;
    uint32_t next_div_min = node->next_div_min;
    
    Tlsf_Allocator_Node* next = _tlsf_alloc_get_node(allocator, next_div_min);
    Tlsf_Allocator_Node* prev = _tlsf_alloc_get_node(allocator, prev_div_min);

    #ifdef TLSF_ALLOC_DEBUG_SLOW
        _tlsf_alloc_check_node(allocator, next, 0);
        _tlsf_alloc_check_node(allocator, prev, 0);
    #endif

    //We try to merge next and prev nodes. They need to be not used and next to each otehr as specified.
    // (In circular list next node can be at the verry beggining of the list, in which case we cant merge).
    // If there are less then 3 nodes thus some of prev/node/next 
    // are actually one and the same we dont care (this case is incredibly rare so we dont optimize for it)
    bool merge_prev = !(prev->packed & TLSF_ALLOC_IS_USED_BIT) && prev_div_min <= node_div_min;
    bool merge_next = !(next->packed & TLSF_ALLOC_IS_USED_BIT) && node_div_min <= next_div_min;
    
    Tlsf_Allocator_Node* merged_node = node;
    uint32_t merged_node_div_min = node_div_min;
    
    int32_t bin_i = 0; 

    //Fast path for no merges
    if(merge_prev == false && merge_next == false)
        bin_i = _tlsf_alloc_unpack(node->packed).bin_index;
    else
    {
        //relink the nodes in adjecency list to skip `node` and `next`
        Tlsf_Allocator_Node* next_next = _tlsf_alloc_get_node(allocator, next->next_div_min);
        #ifdef TLSF_ALLOC_DEBUG_SLOW
            _tlsf_alloc_check_node(allocator, next_next, 0);
        #endif

        uint32_t merged_size_div_min = node->size_div_min;
        if(merge_next)
        {
            allocator->num_nodes -= 1;
            _tlsf_alloc_unlink_node_in_bin(allocator, next, next_div_min, _tlsf_alloc_unpack(next->packed).bin_index);

            //unlink `next`
            node->next_div_min = next->next_div_min;
            next_next->prev_div_min = node_div_min;
            merged_size_div_min += next->size_div_min + sizeof(Tlsf_Allocator_Node)/TLSF_ALLOC_MIN_SIZE;
        }

        if(merge_prev)
        {
            allocator->num_nodes -= 1;
            //Merge next could have happened thus `next` node might actually be `next_next`! We need to reassign
            Tlsf_Allocator_Node* curr_next = _tlsf_alloc_get_node(allocator, node->next_div_min);
        
            _tlsf_alloc_unlink_node_in_bin(allocator, prev, prev_div_min, _tlsf_alloc_unpack(prev->packed).bin_index);
            merged_size_div_min += prev->size_div_min + sizeof(Tlsf_Allocator_Node)/TLSF_ALLOC_MIN_SIZE;

            //Because we need to have contiguous buffer the resulting
            // `merged_node` is the first of the merged nodes `prev`,`node`,`next`.
            // Thus when merging with previous we unlink `node` instead of `prev`!
            prev->next_div_min = node->next_div_min;
            curr_next->prev_div_min = prev_div_min;

            merged_node = prev;
            merged_node_div_min = prev_div_min;
        }
        
        merged_node->size_div_min = merged_size_div_min;
        //merged_node->packed &= ~TLSF_ALLOC_IS_USED_BIT;
        bin_i = _tlsf_alloc_get_bin_floor(merged_size_div_min);
    }
    
    _tlsf_alloc_link_node_in_bin(allocator, merged_node, merged_node_div_min, bin_i);
    
    Tlsf_Allocator_Unpacked unpacked = {0};
    unpacked.bin_index = bin_i;
    merged_node->packed = _tlsf_alloc_pack(unpacked);
    
    #ifdef TLSF_ALLOC_DEBUG_SLOW
        memset(merged_node + 1, -1, node->size_div_min);
    #endif

    _tlsf_alloc_check_node(allocator, merged_node, TLSF_ALLOC_CHECK_UNUSED);
    _tlsf_alloc_check_invariants(allocator);
}

//Just a wrapper around free that also checks other params.

EXPORT void tlsf_alloc_deallocate(Tlsf_Allocator* allocator, void* ptr, isize size, isize align)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(_tlsf_is_pow2_or_zero(align) && align > 0);
    if(ptr == NULL)
        return;

    Tlsf_Allocator_Node* node = _tlsf_alloc_get_allocated_node(allocator, ptr);
    ASSERT(node->size_div_min*TLSF_ALLOC_MIN_SIZE >= size, "Incorrect size provided!");
    ASSERT(ptr == _tlsf_align_forward(ptr, align), "Incorrect align provided!");
    
    tlsf_alloc_free(allocator, ptr);
}

EXPORT void tlsf_alloc_init(Tlsf_Allocator* allocator, void* memory, isize memory_size)
{
    ASSERT(allocator);
    ASSERT(memory_size >= 0);
    memset(allocator, 0, sizeof *allocator);   

    //What are we supposed to do with such a small ammount of memory?!
    if(memory_size < 4*(sizeof(Tlsf_Allocator_Node) + TLSF_ALLOC_MIN_SIZE) || memory == NULL) 
        return;

    allocator->memory = (uint8_t*) memory;
    allocator->memory_size = memory_size/TLSF_ALLOC_MIN_SIZE*TLSF_ALLOC_MIN_SIZE;

    #ifdef TLSF_ALLOC_DEBUG_SLOW
        memset(memory, -1, memory_size);
    #endif

    //Push nill node
    Tlsf_Allocator_Node* nil = _tlsf_alloc_get_node(allocator, 0);
    memset(nil, 0, sizeof *nil);

    //Push first node
    uint32_t first_div_min = sizeof(Tlsf_Allocator_Node)/TLSF_ALLOC_MIN_SIZE;
    first_div_min += 1; //Leave a bit of extra space.

    isize first_size = allocator->memory_size - (isize)first_div_min*TLSF_ALLOC_MIN_SIZE - sizeof(Tlsf_Allocator_Node);
    uint32_t first_size_div_min = (uint32_t)(first_size / TLSF_ALLOC_MIN_SIZE);
    int32_t bin_i = _tlsf_alloc_get_bin_floor(first_size_div_min);

    Tlsf_Allocator_Node* first = _tlsf_alloc_get_node(allocator, first_div_min); 
    first->next_div_min = first_div_min;
    first->prev_div_min = first_div_min;
    first->next_in_bin_div_min = first_div_min;
    first->prev_in_bin_div_min = first_div_min;
    first->size_div_min = first_size_div_min;
    
    Tlsf_Allocator_Unpacked unpacked = {0};
    unpacked.bin_index = bin_i;
    first->packed = _tlsf_alloc_pack(unpacked);

    _tlsf_alloc_link_node_in_bin(allocator, first, first_div_min, bin_i);
    allocator->first_node_div_min = first_div_min;
    allocator->num_nodes = 1;
    
    _tlsf_alloc_check_invariants(allocator);
}

EXPORT void tlsf_alloc_free_all(Tlsf_Allocator* allocator)
{
    tlsf_alloc_init(allocator, allocator->memory, allocator->memory_size);
}

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_TLSF_ALLOCATOR_TEST)) && !defined(JOT_TLSF_ALLOCATOR_HAS_TEST)
#define JOT_TLSF_ALLOCATOR_HAS_TEST

#ifndef STATIC_ARRAY_SIZE
    #define STATIC_ARRAY_SIZE(arr) (isize)(sizeof(arr)/sizeof((arr)[0]))
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

void test_tlsf_alloc_unit()
{
    isize memory_size = 50*1024;
    void* memory = malloc(memory_size);

    Tlsf_Allocator allocator = {0};
    tlsf_alloc_init(&allocator, memory, memory_size);

    struct {
        isize size;
        isize align;
        void* ptr;
    } allocs[4] = {
        {7, 8},
        {16, 8},
        {24, 4},
        {35, 16},
    };

    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
        allocs[i].ptr = tlsf_alloc_allocate(&allocator, allocs[i].size, allocs[i].align);
        
    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
        tlsf_alloc_deallocate(&allocator, allocs[i].ptr, allocs[i].size, allocs[i].align);
        
    free(memory);
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

void test_tlsf_alloc_stress(double seconds, isize at_once)
{
    enum {
        MAX_SIZE_LOG2 = 17, //1/8 MB = 256 KB
        MAX_ALIGN_LOG2 = 5,
        MAX_AT_ONCE = 250,
    };
    const double MAX_PERTURBATION = 0.2;
    
    ASSERT(at_once < MAX_AT_ONCE);
    isize memory_size = 250*1024*1024;
    void* memory = malloc(memory_size);

    Tlsf_Allocator allocator = {0};
    tlsf_alloc_init(&allocator, memory, memory_size);
    
    struct {
        int32_t size;
        int32_t align;
        int32_t pattern;
        int32_t padding;
        void* ptr;
    } allocs[MAX_AT_ONCE] = {0};

    isize iter = 0;
    isize total_size = 0;
    for(double start = _tlsf_clock_s(); _tlsf_clock_s() - start < seconds;)
    {
        isize i = _tlsf_random_range(0, at_once);
        if(iter < at_once)
            i = iter;
        else
        {
            if(allocs[i].ptr != NULL)
                TEST(memtest(allocs[i].ptr, allocs[i].pattern, allocs[i].size));

            tlsf_alloc_deallocate(&allocator, allocs[i].ptr, allocs[i].size, allocs[i].align);
            tlsf_alloc_check_invariants_always(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);
            total_size -= allocs[i].size;
        }
        
        double perturbation = 1 + _tlsf_random_interval(-MAX_PERTURBATION, MAX_PERTURBATION);
        isize random_align_shift = _tlsf_random_range(0, MAX_ALIGN_LOG2);
        isize random_size_shift = _tlsf_random_range(0, MAX_SIZE_LOG2);

        //Random exponentially distributed sizes with small perturbances.
        allocs[i].size = (int32_t)(((isize) 1 << random_size_shift) * perturbation);
        allocs[i].align = (int32_t) ((isize) 1 << random_align_shift);
        allocs[i].pattern = (int32_t) _tlsf_random_range(0, 255);
        allocs[i].ptr = tlsf_alloc_allocate(&allocator, allocs[i].size, allocs[i].align);
        total_size += allocs[i].size;
        
        if(allocs[i].ptr != NULL)
            memset(allocs[i].ptr, allocs[i].pattern, allocs[i].size);

        tlsf_alloc_check_invariants_always(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);

        if(iter > at_once)
        {
            TEST(allocator.bytes_allocated >= total_size);
            TEST(allocator.max_bytes_allocated >= total_size);
        }
        iter += 1;
    }

    free(memory);
}

void test_tlsf_alloc(double seconds)
{
    printf("[TEST]: Tlsf allocator sizes below:\n");
    for(isize i = 0; i < TLSF_ALLOC_BINS; i++)
        printf("[TEST]: %2lli -> %lli\n", i, _tlsf_alloc_ith_bin_size((int32_t) i));

    test_tlsf_alloc_unit();
    test_tlsf_alloc_stress(seconds/4, 1);
    test_tlsf_alloc_stress(seconds/4, 10);
    test_tlsf_alloc_stress(seconds/4, 100);
    test_tlsf_alloc_stress(seconds/4, 200);

    printf("[TEST]: test_tlsf_alloc(%lf) success!\n", seconds);
}

//Include the benchmark only when being included alongside the rest of the codebase
// since I cant be bothered to make it work without any additional includes
#ifdef JOT_ALLOCATOR
    #include "perf.h"
    #include "random.h"
    #include "log.h"
    void benchmark_tlsf_alloc_single(double seconds, isize at_once, isize min_size, isize max_size, isize min_align_log2, isize max_align_log2)
    {
        LOG_INFO("BENCH", "Running benchmarks for %s with at_once:%lli size:[%lli, %lli) align_log:[%lli %lli)", 
            format_seconds(seconds).data, at_once, min_size, max_size, min_align_log2, max_align_log2);

        enum {
            CACHED_COUNT = 1024,
            MAX_AT_ONCE = 1024,
        };
        typedef struct Alloc {
            int32_t size;
            int32_t align;
            void* ptr;
        } Alloc;

        typedef struct Cached_Random {
            int32_t size;
            int32_t align;
            int32_t index;
        } Cached_Random;
    
        isize memory_size = 250*1024*1024;
        void* memory = malloc(memory_size);
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
        tlsf_alloc_init(&tlsf, memory, memory_size);

        Perf_Stats stats_tlsf_alloc = {0};
        Perf_Stats stats_tlsf_free = {0};
    
        Perf_Stats stats_malloc_alloc = {0};
        Perf_Stats stats_malloc_free = {0};

        for(isize j = 0; j < 2; j++)
        {
            bool do_malloc = j > 0;
        
            Perf_Stats* stats_alloc = do_malloc ? &stats_malloc_alloc : &stats_tlsf_alloc;
            Perf_Stats* stats_free = do_malloc ? &stats_malloc_free : &stats_tlsf_free;
            for(Perf_Benchmark bench_alloc = {0}, bench_free = {0}; ;) 
            {
                bool continue1 = perf_benchmark_custom(&bench_alloc, stats_alloc, warmup, seconds, 1);;
                bool continue2 = perf_benchmark_custom(&bench_free, stats_free, warmup, seconds, 1);;
                if(continue1 == false || continue2 == false)
                    break;

                isize iter = bench_alloc.iter;
                Cached_Random random = randoms[iter % CACHED_COUNT];


                isize i = (isize) ((uint64_t) random.index % at_once);
                //At the start only alloc
                if(iter < at_once)
                    i = iter;
                else
                {
                    i64 before_free = perf_now();
                    if(do_malloc)
                        free(allocs[i].ptr);
                    else
                        tlsf_alloc_deallocate(&tlsf, allocs[i].ptr, allocs[i].size, allocs[i].align);
                    i64 after_free = perf_now();
                    perf_benchmark_submit(&bench_free, after_free - before_free);
              
                }  

                allocs[i].ptr = NULL;
                allocs[i].size = random.size;
                allocs[i].align = random.align;
                i64 before_alloc = perf_now();
                if(do_malloc)
                    allocs[i].ptr = malloc(allocs[i].size);
                else
                    allocs[i].ptr = tlsf_alloc_allocate(&tlsf, allocs[i].size, allocs[i].align);
                i64 after_alloc = perf_now();

                if(iter >= at_once)
                    perf_benchmark_submit(&bench_alloc, after_alloc - before_alloc);
            }
        }
    
        free(memory);
        free(allocs);
        free(randoms);

        log_perf_stats_hdr("BENCH", LOG_INFO, "              ");
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf alloc:   ", stats_tlsf_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc alloc: ", stats_malloc_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf free:    ", stats_tlsf_free);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc free:  ", stats_malloc_free);
    }

    void benchmark_tlsf_alloc(double seconds)
    {
        benchmark_tlsf_alloc_single(seconds/4, 4096, 8, 64, 0, 4);
        benchmark_tlsf_alloc_single(seconds/4, 1024, 64, 512, 0, 4);
        benchmark_tlsf_alloc_single(seconds/4, 1024, 8, 64, 0, 4);
        benchmark_tlsf_alloc_single(seconds/4, 128, 64, 512, 0, 4);
    }
    #endif
#endif