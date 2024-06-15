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
#define POOL_ALLOC_MIN_SIZE_LOG2        3
#define POOL_ALLOC_MAX_SIZE             ((uint64_t) UINT32_MAX * POOL_ALLOC_MIN_SIZE)
#define POOL_ALLOC_BINS                 64
#define POOL_ALLOC_MAX_ALIGN            4096 //One page 
#define POOL_ALLOC_PACKED_BIN_BITS      9
#define POOL_ALLOC_PACKED_BIN_OFFSET    21
#define POOL_ALLOC_IS_USED_BIT          ((uint32_t) 1 << 31)
#define POOL_ALLOC_IS_MARKED_BIT        ((uint32_t) 1 << 30)

#define POOL_ALLOC_CHECK_UNUSED     ((uint32_t) 1 << 0)
#define POOL_ALLOC_CHECK_USED       ((uint32_t) 1 << 1)
#define POOL_ALLOC_CHECK_DETAILED   ((uint32_t) 1 << 2)
#define POOL_ALLOC_CHECK_ALL_NODES  ((uint32_t) 1 << 3)
#define POOL_ALLOC_CHECK_BIN        ((uint32_t) 1 << 4)

#ifndef NDEBUG
    #define POOL_ALLOC_DEBUG            //Enables basic safery checks on passed in nodes. Helps to find overwrites
    #define POOL_ALLOC_DEBUG_SLOW       //Enebles extensive checks on nodes. Also fills data to garavge on alloc/free.
    #define POOL_ALLOC_DEBUG_SLOW_SLOW  //Checks all nodes on every entry and before return of every function. Is extremely slow and should only be used when testing this allocator
#endif

#define POOL_ALLOC_NIL              0xFFFFFFFFU
#define POOL_ALLOC_BINS_PER_BUCKET 8
#define POOL_ALLOC_BINS_MASK        (1 << 8) - 1
#define POOL_ALLOC_NODES_PER_BUCKET 32
#define POOL_ALLOC_NODES_MASK       0xFFFFFFFFU

typedef struct Pool_Allocator_Bin_Info {
    uint32_t first_not_filled_bucket;
    uint32_t first_filled_bucket;
    //uint32_t num_free;
    //uint32_t total;
} Pool_Allocator_Bin_Info;

typedef struct Pool_Allocator_Node {
    uint32_t next; // either next in order or next in freelist
    uint32_t prev; // 0xFFFFFFFF when free (debug only)
    uint32_t bin; // 0xFFFFFFFF when free
    uint32_t offset; 
} Pool_Allocator_Node;

typedef struct Pool_Allocator_Bin_Bucket {
    uint32_t next;
    bool visited;
    uint8_t bin_info_index;
    uint16_t mask;
    uint32_t node_i[POOL_ALLOC_BINS_PER_BUCKET];
} Pool_Allocator_Bin_Bucket;

typedef struct Pool_Allocator {
    //i-th bit indicates wheter theres at least single space in i-th bin
    //0-th bin has size POOL_ALLOC_MIN_SIZE
    //63-th bin has size POOL_ALLOC_MAX_SIZE
    uint64_t non_filled_bins;
    isize memory_size;
    uint8_t* memory; //can be null in which case the allocator is in 'GPU' mode
    
    uint32_t bin_bucket_first_free;
    uint32_t bin_bucket_count;
    uint32_t bin_bucket_capacity;

    uint32_t node_first_free;
    uint32_t node_capacity;
    uint32_t node_count;
    Pool_Allocator_Bin_Bucket* bin_buckets;
    Pool_Allocator_Node* nodes;
    
    bool dont_collect_stats; //can be freely toggled at any point in time. Defaults to false
    bool padding[7];
    isize sum_bytes_allocated;
    isize max_bytes_allocated;
    isize bytes_allocated;
    uint32_t max_node_count; 
    uint32_t max_bin_bucket_count;
    isize sum_node_count;
    isize sum_bin_bucket_count;
    isize allocation_count;
    isize deallocation_count;

    Pool_Allocator_Bin_Info bin_info[POOL_ALLOC_BINS];

} Pool_Allocator;


EXPORT void pool_alloc_init(Pool_Allocator* allocator, void* memory, isize memory_size, isize user_node_count);
EXPORT uint32_t pool_alloc_allocate(Pool_Allocator* allocator, isize size, isize align);
EXPORT void pool_alloc_deallocate(Pool_Allocator* allocator, uint32_t id);
EXPORT void* pool_alloc_malloc(Pool_Allocator* allocator, isize size, isize align);
EXPORT void pool_alloc_free(Pool_Allocator* allocator, void* ptr);
EXPORT void pool_alloc_free_all(Pool_Allocator* allocator);

//Checks wheter the allocator is in valid state. If is not aborts.
// Flags can be POOL_ALLOC_CHECK_DETAILED and POOL_ALLOC_CHECK_ALL_NODES.
EXPORT void pool_alloc_check_invariants_always(Pool_Allocator* allocator, uint32_t flags);

#endif

//#define JOT_ALL_IMPL
//#define JOT_ALL_TEST

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

INTERNAL int32_t _pool_alloc_get_bin_floor(uint32_t size_div_min)
{
    ASSERT(size_div_min >= 0);

    //Effectively computes floor(log_beta(size/M)) where 
    // beta is the logarithm base equal to sqrt(2) and M = POOL_ALLOC_MIN_SIZE_LOG2.
    // floor(log_beta(size)) = floor(log2(size/M)/log2(sqrt(2)) = 
    //                       = floor(log2(size/M)/0.5)
    //                       = floor(2*log2(size/M))
    //                       = floor(2*log2(size)) - 2*log2(M)
    //
    int32_t lower_bound_log2 = _pool_find_last_set_bit32(size_div_min);
    uint32_t lower_bound = (uint32_t) 1 << lower_bound_log2;
    uint32_t middle_point_offset = (uint32_t) 1 << (lower_bound_log2-1);

    int32_t res = 2*(int32_t)lower_bound_log2 + (int32_t) (size_div_min >= lower_bound + middle_point_offset);
    return res;
}

INTERNAL int32_t _pool_alloc_get_bin_ceil(uint32_t size_div_min)
{
    int32_t index = _pool_alloc_get_bin_floor(size_div_min);

    // Unless its power of two (thus 1 << lower_bound_log2 == size_div_min) we take the next bin!
    return index + (int32_t) !_pool_is_pow2_or_zero(size_div_min); 
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

INTERNAL void _pool_alloc_check_node_always(Pool_Allocator* allocator, uint32_t node_i, uint32_t flags)
{
    //Must not be priviledged node or be out of range
    TEST(2 <= node_i && node_i < allocator->node_capacity);
    Pool_Allocator_Node* node = &allocator->nodes[node_i];
    if(flags & POOL_ALLOC_CHECK_USED)
        TEST(node->bin == 0xFFFFFFFF);
    if(flags & POOL_ALLOC_CHECK_UNUSED)
        TEST(node->bin != 0xFFFFFFFF);

    //Need to have valid indexes. Must not point to itself
    TEST(node->bin == 0xFFFFFFFF || node->bin/POOL_ALLOC_BINS_PER_BUCKET <= allocator->bin_bucket_capacity);
    TEST(node->offset <= allocator->memory_size);
    TEST(node->prev <= allocator->node_capacity && node->prev != node_i);
    TEST(node->next <= allocator->node_capacity && node->next != node_i);

    if(flags & POOL_ALLOC_CHECK_DETAILED)
    {
        Pool_Allocator_Node* prev = &allocator->nodes[node->prev];
        Pool_Allocator_Node* next = &allocator->nodes[node->next];

        //Need to be ordered
        TEST(prev->offset <= node->offset);
        TEST(node->offset <= next->offset);

        //Need to be properly linked
        TEST(next->prev == node_i);
        TEST(prev->next == node_i);

        if(node->bin != 0xFFFFFFFF)
        {
            Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[node->bin / POOL_ALLOC_BINS_PER_BUCKET];
            uint32_t node_retrieved_i = bin_bucket->node_i[node->bin % POOL_ALLOC_BINS_PER_BUCKET];
            TEST(node_retrieved_i == node_i);
            
            uint32_t node_size = next->offset - node->offset;
            int32_t node_bin_info_i = _pool_alloc_get_bin_floor(node_size);
            TEST((int32_t) bin_bucket->bin_info_index == node_bin_info_i);
            TEST((int32_t) bin_bucket->bin_info_index == node_bin_info_i);
        }
    }
}

INTERNAL void pool_alloc_check_bin_block_invariants(Pool_Allocator* allocator)
{
    //Check bin buckets linked lists
    uint32_t used_bin_buckets = 0;
    typedef struct Bin_Info_Info {
        uint32_t filled_count;
        uint32_t non_filled_count;
    } Bin_Info_Info;

    Bin_Info_Info infos[POOL_ALLOC_BINS] = {0};

    for(isize i = 0; i < allocator->bin_bucket_capacity; i++)
        allocator->bin_buckets[i].visited = false;

    for(uint32_t bin_info_i = 0; bin_info_i < POOL_ALLOC_BINS; bin_info_i++)
    {
        Pool_Allocator_Bin_Info* bin_info = &allocator->bin_info[bin_info_i];
        uint32_t filled_count = 0; 
        for(uint32_t bin_bucket_i = bin_info->first_filled_bucket; bin_bucket_i != 0xFFFFFFFF; filled_count++)
        {
            TEST(bin_bucket_i < allocator->bin_bucket_capacity);
            Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
            TEST(filled_count < allocator->bin_bucket_count);
            TEST(bin_bucket->mask == POOL_ALLOC_BINS_MASK);
            TEST(bin_bucket->bin_info_index == bin_info_i);
            TEST(bin_bucket->visited == false);
            bin_bucket->visited = true;
            for(uint32_t i = 0; i < POOL_ALLOC_BINS_PER_BUCKET; i++)
                TEST(bin_bucket->node_i[i] != 0xFFFFFFFF);

            bin_bucket_i = bin_bucket->next;
        }
            
        uint32_t non_filled_count = 0; 
        for(uint32_t bin_bucket_i = bin_info->first_not_filled_bucket; bin_bucket_i != 0xFFFFFFFF; non_filled_count++)
        {
            TEST(bin_bucket_i < allocator->bin_bucket_capacity);
            Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
            TEST(non_filled_count < allocator->bin_bucket_count);
            TEST(bin_bucket->mask != POOL_ALLOC_BINS_MASK && bin_bucket->mask != 0);
            TEST(bin_bucket->bin_info_index == bin_info_i);
            TEST(bin_bucket->visited == false);
            bin_bucket->visited = true;
            for(uint32_t i = 0; i < POOL_ALLOC_BINS_PER_BUCKET; i++)
            {
                bool is_used = !!(bin_bucket->mask & (1U << i));
                TEST((bin_bucket->node_i[i] != 0xFFFFFFFF) == is_used);
                int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
            }

            bin_bucket_i = bin_bucket->next;
        }

        infos[bin_info_i].filled_count = filled_count;
        infos[bin_info_i].non_filled_count = non_filled_count;
        used_bin_buckets += filled_count + non_filled_count;
    }

    //Check bin bucket free list
    uint32_t free_bucket_count = 0; 
    for(uint32_t bin_bucket_i = allocator->bin_bucket_first_free; bin_bucket_i != 0xFFFFFFFF; free_bucket_count++)
    {
        TEST(bin_bucket_i < allocator->bin_bucket_capacity);
        Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
        TEST(free_bucket_count < allocator->bin_bucket_capacity - allocator->bin_bucket_count);
        TEST(bin_bucket->mask == 0);
        TEST(bin_bucket->bin_info_index == 0x55);
        TEST(bin_bucket->visited == false);
        bin_bucket->visited = true;
        for(uint32_t i = 0; i < POOL_ALLOC_BINS_PER_BUCKET; i++)
            TEST(bin_bucket->node_i[i] == 0x55555555);

        bin_bucket_i = bin_bucket->next;
    }
        
    for(isize i = 0; i < allocator->bin_bucket_capacity; i++)
        TEST(allocator->bin_buckets[i].visited);

    TEST(used_bin_buckets == allocator->bin_bucket_count);
    TEST(free_bucket_count == allocator->bin_bucket_capacity - allocator->bin_bucket_count);
}

EXPORT void pool_alloc_check_invariants_always(Pool_Allocator* allocator, uint32_t flags)
{
    //Check if bin free lists match the mask
    for(int32_t i = 0; i < POOL_ALLOC_BINS; i++)
    {
        bool has_ith_bin = allocator->bin_info[i].first_filled_bucket != 0xFFFFFFFF 
            || allocator->bin_info[i].first_not_filled_bucket != 0xFFFFFFFF;
        uint64_t ith_bit = (uint64_t) 1 << i;
        TEST(!!(allocator->non_filled_bins & ith_bit) == has_ith_bin);
        int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
    }

    //Check validity of core stats
    TEST(allocator->bin_buckets != NULL);
    TEST(allocator->nodes != NULL);
    TEST(allocator->bin_bucket_count <= allocator->bin_bucket_capacity);
    TEST(allocator->node_count <= allocator->node_capacity);

    //Check validity of all stats
    if(allocator->dont_collect_stats == false)
    {
        TEST(allocator->bytes_allocated <= allocator->max_bytes_allocated);
        TEST(allocator->allocation_count >= 0);
        TEST(allocator->deallocation_count >= 0 && allocator->deallocation_count <= allocator->allocation_count);

        TEST(allocator->bin_bucket_count <= allocator->max_bin_bucket_count);
        TEST(allocator->node_count <= allocator->max_node_count);
        
        TEST(allocator->sum_bytes_allocated >= 0);
        TEST(allocator->sum_node_count >= 0);
        TEST(allocator->sum_bin_bucket_count >= 0);
    }

    //Check START and END node
    ASSERT(allocator->node_capacity >= 3);
    uint32_t start_i = 0;
    uint32_t end_i = 1;

    Pool_Allocator_Node* start = &allocator->nodes[start_i];
    Pool_Allocator_Node* end = &allocator->nodes[end_i];

    TEST(start->bin == 0xFFFFFFFF);
    TEST(start->prev == 0xFFFFFFFF);
    TEST(start->offset == 0);
    
    TEST(end->bin == 0xFFFFFFFF);
    TEST(end->next == 0xFFFFFFFF);
    TEST(end->offset == allocator->memory_size);
        
    if(flags & POOL_ALLOC_CHECK_ALL_NODES)
    {
        pool_alloc_check_bin_block_invariants(allocator);

        //Check node free list
        uint32_t free_node_count = 0; 
        for(uint32_t node_i = allocator->node_first_free; node_i != 0xFFFFFFFF; free_node_count++)
        {
            TEST(2 <= node_i && node_i < allocator->node_capacity);
            Pool_Allocator_Node* node = &allocator->nodes[node_i];
            TEST(free_node_count < allocator->node_capacity - allocator->node_count);
            TEST(node->prev == 0x55555555);
            TEST(node->bin == 0x55555555);
            TEST(node->offset == 0x55555555);
            node_i = node->next;
        }
        TEST(free_node_count == allocator->node_capacity - allocator->node_count - 2);

        //Go through all nodes
        uint32_t counted_nodes = 0;
        for(uint32_t node_i = start->next; node_i != end_i; node_i = allocator->nodes[node_i].next)
        {
            counted_nodes += 1;
            TEST(counted_nodes <= allocator->node_count);
            _pool_alloc_check_node_always(allocator, node_i, flags);
        }

        TEST(counted_nodes == allocator->node_count);
    }
}

INTERNAL void _pool_alloc_check_node(Pool_Allocator* allocator, uint32_t node_i, uint32_t flags)
{
    #ifdef POOL_ALLOC_DEBUG
        #ifdef POOL_ALLOC_DEBUG_SLOW
            flags |= POOL_ALLOC_CHECK_DETAILED;
        #else
            flags &= ~POOL_ALLOC_CHECK_DETAILED;
        #endif

        _pool_alloc_check_node_always(allocator, node_i, flags);
    #endif
}

INTERNAL void _pool_alloc_check_invariants(Pool_Allocator* allocator)
{
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

INTERNAL void pool_alloc_bin_remove_bucket(Pool_Allocator* allocator, int32_t bin_info_i, uint32_t bin_bucket_i, uint32_t bin_offset_i)
{
    pool_alloc_check_bin_block_invariants(allocator);
    Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
    Pool_Allocator_Bin_Info* bin_info = &allocator->bin_info[bin_info_i];
    
    if(_pool_is_pow2_or_zero(bin_bucket->mask) == false && bin_bucket->mask != POOL_ALLOC_BINS_MASK)
    {
        printf("Unlink of bin:%i|%i bin_info_i:%i\n", bin_bucket_i, bin_offset_i, bin_info_i);
    }

    //TODO debug only!
    bin_bucket->node_i[bin_offset_i] = 0xFFFFFFFF;

    //Clear bit of this bin. 
    //If the bucket had POOL_ALLOC_BINS_MASK mask (was full) then relink it to the partially filled buckets
    //If the entire bucket is now unused relink to the allocator-global freelist of bin buckets
    if(bin_bucket->mask == POOL_ALLOC_BINS_MASK)
    {
        printf("Unlink of bin:%i|%i bin_info_i:%i FULL\n", bin_bucket_i, bin_offset_i, bin_info_i);

        bin_info->first_filled_bucket = bin_bucket->next;
        bin_bucket->next = bin_info->first_not_filled_bucket;
        bin_info->first_not_filled_bucket = bin_bucket_i;
    }

    bin_bucket->mask &= ~(1 << bin_offset_i);
    if(bin_bucket->mask == 0)
    {
        printf("Unlink of bin:%i|%i bin_info_i:%i EMPTY\n", bin_bucket_i, bin_offset_i, bin_info_i);

        #define DUMPI(x) printf("> " #x ":%i\n", x);

        DUMPI(allocator->bin_bucket_first_free);
        DUMPI(bin_bucket->next);
        DUMPI(bin_info->first_not_filled_bucket);

        //Unlink
        bin_info->first_not_filled_bucket = bin_bucket->next;

        //Link
        bin_bucket->next = allocator->bin_bucket_first_free;
        allocator->bin_bucket_first_free = bin_bucket_i;
        allocator->bin_bucket_count -= 1;

        printf("AFTER:\n");
        DUMPI(allocator->bin_bucket_first_free);
        DUMPI(bin_bucket->next);
        DUMPI(bin_info->first_not_filled_bucket);

        //TODO debug only!
        bin_bucket->mask = 0;
        bin_bucket->bin_info_index = 0x55;
        for(uint32_t i = 0; i < POOL_ALLOC_BINS_PER_BUCKET; i++)
            bin_bucket->node_i[i] = 0x55555555;

        //If there are no more bins left and even the filled buckets are gone 
        // remove this entire bin info from the availible
        if(bin_info->first_not_filled_bucket == 0xFFFFFFFF && bin_info->first_filled_bucket == 0xFFFFFFFF)
        {
            printf("Retiring bin_info_i:%i \n", bin_info_i);
            allocator->non_filled_bins &= ~((uint64_t) 1 << bin_info_i);
        }
    }
    
    pool_alloc_check_bin_block_invariants(allocator);
}


typedef struct Pool_Alloc_Remove_Bin {
    uint32_t node;
    uint32_t bin;
} Pool_Alloc_Remove_Bin;

INTERNAL Pool_Alloc_Remove_Bin pool_alloc_bin_get_free_node(Pool_Allocator* allocator, int32_t bin_info_i)
{
    ASSERT(0 <= bin_info_i && bin_info_i <= POOL_ALLOC_BINS);
    Pool_Allocator_Bin_Info* bin_info = &allocator->bin_info[bin_info_i];

    //Get an appropriate bin bucket. 
    //First we try to get from bucket we are currently working in
    // ie. the first_not_filled_bucket.
    //Then we try a bucket with all filled slots and we link it to the non filled buckets
    if(bin_info->first_not_filled_bucket != 0xFFFFFFFF)
    {
        uint32_t bin_bucket_i = bin_info->first_not_filled_bucket;
        ASSERT(bin_bucket_i < allocator->bin_bucket_capacity);

        Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
        uint32_t bin_offset = _pool_find_first_set_bit64(bin_bucket->mask & POOL_ALLOC_BINS_MASK);
        ASSERT(bin_offset < POOL_ALLOC_BINS_PER_BUCKET);
        
        uint32_t node_i = bin_bucket->node_i[bin_offset];
        ASSERT(2 <= node_i && node_i < allocator->node_capacity);

        pool_alloc_bin_remove_bucket(allocator, bin_info_i, bin_bucket_i, bin_offset);

        Pool_Alloc_Remove_Bin out = {node_i, bin_bucket_i*POOL_ALLOC_BINS_PER_BUCKET + bin_offset};
        return out;
    }
    else 
    {
        uint32_t bin_bucket_i = bin_info->first_filled_bucket;
        ASSERT(bin_bucket_i != 0xFFFFFFFF && bin_bucket_i < allocator->bin_bucket_capacity);

        Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
        ASSERT(bin_bucket->mask == POOL_ALLOC_BINS_MASK);
        
        uint32_t node_i = bin_bucket->node_i[0];
        ASSERT(2 <= node_i && node_i < allocator->node_capacity);
        
        //Note debug only
        bin_bucket->node_i[0] = 0xFFFFFFFF;

        //Relink to first_not_filled_bucket
        bin_bucket->mask &= ~1U;

        //Unlink
        bin_info->first_filled_bucket = bin_bucket->next;

        //Link
        bin_bucket->next = bin_info->first_not_filled_bucket; //bin_info->first_not_filled_bucket == 0xFFFFFFFF
        bin_info->first_not_filled_bucket = bin_bucket_i;
        
        Pool_Alloc_Remove_Bin out = {node_i, bin_bucket_i*POOL_ALLOC_BINS_PER_BUCKET + 0};
        return out;
    }
}

typedef struct Pool_Alloc_Add_Bin {
    uint32_t bin;
} Pool_Alloc_Add_Bin;

INTERNAL Pool_Alloc_Add_Bin pool_alloc_bin_add_free_node(Pool_Allocator* allocator, int32_t bin_info_i, uint32_t node_i)
{
    ASSERT(2 <= node_i && node_i < allocator->node_capacity);
    ASSERT(0 <= bin_info_i && bin_info_i <= POOL_ALLOC_BINS);

    allocator->non_filled_bins |= (uint64_t) 1 << bin_info_i;
    Pool_Allocator_Bin_Info* bin_info = &allocator->bin_info[bin_info_i];
    if(bin_info->first_not_filled_bucket != 0xFFFFFFFF)
    {
        uint32_t bin_bucket_i = bin_info->first_not_filled_bucket;
        ASSERT(bin_bucket_i < allocator->bin_bucket_capacity);

        Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
        uint32_t bin_offset_i = _pool_find_first_set_bit64(~bin_bucket->mask & POOL_ALLOC_BINS_MASK);
        ASSERT(bin_offset_i < POOL_ALLOC_BINS_PER_BUCKET);
    
        bin_bucket->node_i[bin_offset_i] = node_i;
        bin_bucket->mask |= (1U << bin_offset_i);

        //If full relink to the first_filled_bucket list
        if(bin_bucket->mask == POOL_ALLOC_BINS_MASK) 
        {
            //Unlink
            bin_info->first_not_filled_bucket = bin_bucket->next;
            
            //Link
            bin_bucket->next = bin_info->first_filled_bucket;
            bin_info->first_filled_bucket = bin_bucket_i;
        }

        Pool_Alloc_Add_Bin out = {bin_bucket_i*POOL_ALLOC_BINS_PER_BUCKET + bin_offset_i};
        return out;
    }
    else
    {
        uint32_t bin_bucket_i = allocator->bin_bucket_first_free;
        ASSERT(bin_bucket_i < allocator->bin_bucket_capacity);

        Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
        allocator->bin_bucket_first_free = bin_bucket->next;
        allocator->bin_bucket_count += 1;
        ASSERT(bin_bucket->mask == 0);
        
        printf("Bin bin_bucket_count:%i bin_info_i:%i first_free:%i UP\n", allocator->bin_bucket_count, bin_info_i, allocator->bin_bucket_first_free);
        uint32_t bin_offset_i = 0;
        //TODO: debug only!
        for(uint32_t i = 0; i < POOL_ALLOC_BINS_PER_BUCKET; i++)
            bin_bucket->node_i[i] = 0xFFFFFFFF;

        bin_bucket->bin_info_index = (uint8_t) bin_info_i;
        bin_bucket->node_i[bin_offset_i] = node_i;
        bin_bucket->mask = 1;

        bin_bucket->next = bin_info->first_not_filled_bucket; //0xFFFFFFFF
        bin_info->first_not_filled_bucket = bin_bucket_i;

        Pool_Alloc_Add_Bin out = {bin_bucket_i*POOL_ALLOC_BINS_PER_BUCKET + bin_offset_i};
        return out;
    }
}


INTERNAL void pool_alloc_update_sats(Pool_Allocator* allocator, isize bytes_allocated_delta)
{
    if(allocator->dont_collect_stats == false)
    {
        #define POOL_ALLOC_MAX(a, b) ((a) > (b) ? (a) : (b))

        allocator->bytes_allocated += bytes_allocated_delta;
        allocator->max_bytes_allocated = POOL_ALLOC_MAX(allocator->max_bytes_allocated, allocator->bytes_allocated);
        allocator->sum_bytes_allocated += allocator->bytes_allocated;

        allocator->max_node_count = POOL_ALLOC_MAX(allocator->max_node_count, allocator->node_count);
        allocator->max_bin_bucket_count = POOL_ALLOC_MAX(allocator->max_bin_bucket_count, allocator->bin_bucket_count);
        
        allocator->sum_node_count += allocator->node_count;
        allocator->sum_bin_bucket_count += allocator->bin_bucket_count;
    }
}

EXPORT uint32_t pool_alloc_allocate(Pool_Allocator* allocator, isize size, isize align)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(size <= UINT32_MAX); //TODO!
    ASSERT(_pool_is_pow2_or_zero(align) && align > 0);

    _pool_alloc_check_invariants(allocator);
    if(size == 0)
        return 0xFFFFFFFF;

    uint32_t adjusted_size = (uint32_t) size;
    uint32_t adjusted_align = POOL_ALLOC_MIN_SIZE;
    if(align > POOL_ALLOC_MIN_SIZE)
    {
        adjusted_align = align < POOL_ALLOC_MAX_ALIGN ? (uint32_t) align : POOL_ALLOC_MAX_ALIGN;
        adjusted_size += adjusted_align;
    }
    
    int32_t bin_from = _pool_alloc_get_bin_ceil(adjusted_size);
    uint64_t bins_mask = ((uint64_t) 1 << bin_from) - 1;
    uint64_t suitable_non_filled_bins = allocator->non_filled_bins & ~bins_mask;
    if(suitable_non_filled_bins == 0)
        return 0xFFFFFFFF;

    int32_t bin_info_i = _pool_find_first_set_bit64(suitable_non_filled_bins);
    Pool_Alloc_Remove_Bin free_node = pool_alloc_bin_get_free_node(allocator, bin_info_i);
    ASSERT(free_node.node < allocator->node_capacity);
    
    Pool_Allocator_Node* __restrict node = &allocator->nodes[free_node.node];
    node->bin = 0xFFFFFFFF;
    _pool_alloc_check_node(allocator, free_node.node, POOL_ALLOC_CHECK_USED);

    Pool_Allocator_Node* __restrict next =  &allocator->nodes[node->next];
    uint32_t node_size = next->offset - node->offset;
    uint32_t rem_size = node_size - adjusted_size;

    if(rem_size >= POOL_ALLOC_MIN_SIZE)
    {
        //Get added node
        uint32_t added_i = allocator->node_first_free;
        ASSERT(2 <= added_i && added_i < allocator->node_capacity);
        Pool_Allocator_Node* __restrict added = &allocator->nodes[added_i];
        allocator->node_first_free = added->next;
        allocator->node_count += 1;
        
        //Get and add to bin
        int32_t added_to_bin_i = _pool_alloc_get_bin_floor(rem_size);
        Pool_Alloc_Add_Bin added_bin = pool_alloc_bin_add_free_node(allocator, added_to_bin_i, added_i);
        
        added->offset = node->offset + adjusted_size;
        added->bin = added_bin.bin;
        added->next = node->next;
        added->prev = free_node.node;
        
        next->prev = added_i;
        node->next = added_i;
        
        _pool_alloc_check_node(allocator, added_i, POOL_ALLOC_CHECK_UNUSED);
        if(added->next >= 2) //If is not STAR or END
            _pool_alloc_check_node(allocator, added->next, 0);
    }
    
    pool_alloc_update_sats(allocator, adjusted_size);

    allocator->allocation_count += 1;
    _pool_alloc_check_node(allocator, free_node.node, POOL_ALLOC_CHECK_USED);
    _pool_alloc_check_invariants(allocator);
    return free_node.node;
}


INTERNAL void pool_alloc_bin_remove_free_node(Pool_Allocator* allocator, int32_t node_i)
{
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_UNUSED);
    Pool_Allocator_Node* __restrict node = &allocator->nodes[node_i];
    Pool_Allocator_Node* __restrict next = &allocator->nodes[node->next];
    Pool_Allocator_Node* __restrict prev = &allocator->nodes[node->prev];

    uint32_t bin_bucket_i = node->bin / POOL_ALLOC_BINS_PER_BUCKET;
    uint32_t bin_offset_i = node->bin % POOL_ALLOC_BINS_PER_BUCKET;
    Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[bin_bucket_i];
    
    //Else do the full remove procedure
    //debug
    uint32_t node_size = next->offset - node->offset;
    int32_t node_bin_info_i = _pool_alloc_get_bin_floor(node_size);
    ASSERT(bin_bucket->bin_info_index == node_bin_info_i);
        
    pool_alloc_bin_remove_bucket(allocator, bin_bucket->bin_info_index, bin_bucket_i, bin_offset_i);
    
    //Unlink
    prev->next = node->next;
    next->prev = node->prev;

    //add to free list
    node->next = allocator->node_first_free;
    allocator->node_first_free = node_i;
    allocator->node_count -= 1;

    //Fill with garbage
    node->prev = 0x55555555;
    node->bin = 0x55555555;
    node->offset = 0x55555555;
}

EXPORT void pool_alloc_deallocate(Pool_Allocator* allocator, uint32_t node_i)
{
    if(node_i == 0xFFFFFFFF)
        return;

    ASSERT(allocator);
    _pool_alloc_check_invariants(allocator);
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_USED);
    Pool_Allocator_Node* __restrict node = &allocator->nodes[node_i];
    Pool_Allocator_Node* __restrict next = &allocator->nodes[node->next];
    Pool_Allocator_Node* __restrict prev = &allocator->nodes[node->prev];
    
    uint32_t old_node_size = next->offset - node->offset;
    uint32_t node_offset = node->offset;
    uint32_t next_offset = next->offset;

    //If prev is free merge
    if(prev->bin != 0xFFFFFFFF)
    {
        printf("Merged prev \n");
        node_offset = prev->offset;
        pool_alloc_bin_remove_free_node(allocator, node->prev);
    }
    //If next is free merge
    if(next->bin != 0xFFFFFFFF)
    {
        printf("Merged next \n");
        pool_alloc_bin_remove_free_node(allocator, node->next);
        next_offset = allocator->nodes[node->next].offset;
    }
    
    uint32_t node_size = next_offset - node_offset;
    int32_t node_bin_info_i = _pool_alloc_get_bin_floor(node_size);
    node->bin = pool_alloc_bin_add_free_node(allocator, node_bin_info_i, node_i).bin;
    node->offset = node_offset;
    _pool_alloc_check_node(allocator, node_i, POOL_ALLOC_CHECK_UNUSED);
    
    pool_alloc_check_bin_block_invariants(allocator);

    pool_alloc_update_sats(allocator, -(isize) old_node_size);
    allocator->deallocation_count += 1;
    _pool_alloc_check_invariants(allocator);
}

EXPORT void pool_alloc_init(Pool_Allocator* allocator, void* memory, isize memory_size, isize user_node_count)
{
    ASSERT(allocator);
    ASSERT(memory_size >= 0);
    memset(allocator, 0, sizeof *allocator);   

    allocator->memory = (uint8_t*) memory;
    allocator->memory_size = memory_size/POOL_ALLOC_MIN_SIZE*POOL_ALLOC_MIN_SIZE;

    #ifdef POOL_ALLOC_DEBUG_SLOW
    if(memory)
        memset(memory, 0x55555555, memory_size);
    #endif

    uint32_t node_count = 3 + (uint32_t) user_node_count;
    uint32_t bin_bucket_count = ((uint32_t) user_node_count + POOL_ALLOC_BINS_PER_BUCKET - 1) / POOL_ALLOC_BINS_PER_BUCKET;
    //Account for max waste (+1 to be safe)
    bin_bucket_count += POOL_ALLOC_BINS + 1;

    allocator->nodes = (Pool_Allocator_Node*) malloc((size_t) node_count * sizeof(Pool_Allocator_Node));
    allocator->bin_buckets = (Pool_Allocator_Bin_Bucket*) malloc((size_t) bin_bucket_count * sizeof(Pool_Allocator_Bin_Bucket));

    ASSERT(allocator->nodes && allocator->bin_buckets);

    //TODO: check allocation failiure! //TODO: debug only!
    memset(allocator->nodes, 0x55555555, node_count * sizeof(Pool_Allocator_Node));
    memset(allocator->bin_buckets, 0x55555555, bin_bucket_count * sizeof(Pool_Allocator_Bin_Bucket));

    allocator->node_capacity = node_count;
    allocator->bin_bucket_capacity = bin_bucket_count;

    allocator->bin_bucket_first_free = 0xFFFFFFFF;
    allocator->node_first_free = 0xFFFFFFFF;

    //Insert nodes and buckets backwards
    for(uint32_t i = node_count; i-- > 0;)
    {
        Pool_Allocator_Node* node = &allocator->nodes[i];
        node->next = allocator->node_first_free;
        allocator->node_first_free = i;
    }
    
    for(uint32_t i = bin_bucket_count; i-- > 0;)
    {
        Pool_Allocator_Bin_Bucket* bin_bucket = &allocator->bin_buckets[i];
        for(uint32_t k = 0; k < POOL_ALLOC_BINS_PER_BUCKET; k++)
            bin_bucket->node_i[k] = 0x55555555;

        bin_bucket->mask = 0;
        bin_bucket->bin_info_index = 0x55;
        bin_bucket->next = allocator->bin_bucket_first_free;
        allocator->bin_bucket_first_free = i;
    }
    
    for(uint32_t i = 0; i < POOL_ALLOC_BINS; i++)
    {
        allocator->bin_info[i].first_filled_bucket = 0xFFFFFFFF;
        allocator->bin_info[i].first_not_filled_bucket = 0xFFFFFFFF;
    }

    //Insert START and END nodes.
    // These nodes are priviledged and are in place to skip the need for excessive ifs.
    uint32_t start_i = allocator->node_first_free;
    Pool_Allocator_Node* start = &allocator->nodes[start_i];
    allocator->node_first_free = start->next;

    uint32_t end_i = allocator->node_first_free;
    Pool_Allocator_Node* end = &allocator->nodes[end_i];
    allocator->node_first_free = end->next;
    
    //Insert an empty content node. This node will be split up into the rest of the nodes in the allocator.
    uint32_t first_i = allocator->node_first_free;
    Pool_Allocator_Node* first = &allocator->nodes[first_i];
    allocator->node_first_free = first->next;

    ASSERT(start_i == 0);
    ASSERT(end_i == 1);

    start->prev = 0xFFFFFFFF;
    start->next = first_i;
    start->offset = 0;
    start->bin = 0xFFFFFFFF;

    end->prev = first_i;
    end->next = 0xFFFFFFFF;
    end->offset = (uint32_t) memory_size;
    end->bin = 0xFFFFFFFF;
    
    int32_t first_bin_info_i = _pool_alloc_get_bin_floor((uint32_t) memory_size);
    first->prev = start_i;
    first->next = end_i;
    first->offset = 0;
    first->bin = pool_alloc_bin_add_free_node(allocator, first_bin_info_i, first_i).bin;
    allocator->node_count += 1;
    pool_alloc_update_sats(allocator, 0);
    
    _pool_alloc_check_invariants(allocator);
}

//EXPORT void* pool_alloc_malloc(Pool_Allocator* allocator, isize size, isize align)
//{
//
//}
//
//EXPORT void pool_alloc_reset(Pool_Allocator* allocator)
//{
//    pool_alloc_init(allocator, allocator->memory, allocator->memory_size);
//}

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
    void* memory = NULL;

    Pool_Allocator allocator = {0};
    pool_alloc_init(&allocator, memory, memory_size, 1024);

    struct {
        uint32_t size;
        uint32_t align;
        uint32_t alloc;
    } allocs[4] = {
        {7, 8},
        {16, 8},
        {24, 4},
        {35, 16},
    };

    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
        allocs[i].alloc = pool_alloc_allocate(&allocator, allocs[i].size, allocs[i].align);
        
    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
        pool_alloc_deallocate(&allocator, allocs[i].alloc);
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
        MAX_AT_ONCE = 250,
    };
    const double MAX_PERTURBATION = 0.2;
    
    ASSERT(at_once < MAX_AT_ONCE);
    isize memory_size = 250*1024*1024;
    void* memory = NULL;

    Pool_Allocator allocator = {0};
    pool_alloc_init(&allocator, memory, memory_size, 1024);
    
    struct {
        int32_t size;
        int32_t align;
        uint32_t alloc;
    } allocs[MAX_AT_ONCE] = {0};

    isize iter = 0;
    for(double start = _pool_clock_s(); _pool_clock_s() - start < seconds;)
    {
        isize i = _pool_random_range(0, at_once);
        if(iter < at_once)
            i = iter;
        else
        {
            pool_alloc_deallocate(&allocator, allocs[i].alloc);
            pool_alloc_check_invariants_always(&allocator, POOL_ALLOC_CHECK_DETAILED | POOL_ALLOC_CHECK_ALL_NODES);
        }
        
        double perturbation = 1 + _pool_random_interval(-MAX_PERTURBATION, MAX_PERTURBATION);
        isize random_align_shift = _pool_random_range(0, MAX_ALIGN_LOG2);
        isize random_size_shift = _pool_random_range(0, MAX_SIZE_LOG2);

        //Random exponentially distributed sizes with small perturbances.
        allocs[i].size = (int32_t)(((isize) 1 << random_size_shift) * perturbation);
        allocs[i].align = (int32_t) ((isize) 1 << random_align_shift);
        allocs[i].alloc = pool_alloc_allocate(&allocator, allocs[i].size, allocs[i].align);

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
#if 0
    #include "perf.h"
    #include "random.h"
    #include "log.h"
    void benchmark_pool_alloc_single(double seconds, isize at_once, isize min_size, isize max_size, isize min_align_log2, isize max_align_log2)
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

        Pool_Allocator pool = {0};
        pool_alloc_init(&pool, memory, memory_size);

        Perf_Stats stats_pool_alloc = {0};
        Perf_Stats stats_pool_free = {0};
    
        Perf_Stats stats_malloc_alloc = {0};
        Perf_Stats stats_malloc_free = {0};

        for(isize j = 0; j < 2; j++)
        {
            bool do_malloc = j > 0;
        
            Perf_Stats* stats_alloc = do_malloc ? &stats_malloc_alloc : &stats_pool_alloc;
            Perf_Stats* stats_free = do_malloc ? &stats_malloc_free : &stats_pool_free;
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
                        pool_alloc_deallocate(&pool, allocs[i].ptr, allocs[i].size, allocs[i].align);
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
                    allocs[i].ptr = pool_alloc_allocate(&pool, allocs[i].size, allocs[i].align);
                i64 after_alloc = perf_now();

                if(iter >= at_once)
                    perf_benchmark_submit(&bench_alloc, after_alloc - before_alloc);
            }
        }
    
        free(memory);
        free(allocs);
        free(randoms);

        log_perf_stats_hdr("BENCH", LOG_INFO, "              ");
        log_perf_stats_row("BENCH", LOG_INFO, "pool alloc:   ", stats_pool_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc alloc: ", stats_malloc_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "pool free:    ", stats_pool_free);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc free:  ", stats_malloc_free);
    }

    void benchmark_pool_alloc(double seconds)
    {
        benchmark_pool_alloc_single(seconds/4, 4096, 8, 64, 0, 4);
        benchmark_pool_alloc_single(seconds/4, 1024, 64, 512, 0, 4);
        benchmark_pool_alloc_single(seconds/4, 1024, 8, 64, 0, 4);
        benchmark_pool_alloc_single(seconds/4, 128, 64, 512, 0, 4);
    }
    #endif
#endif
#endif