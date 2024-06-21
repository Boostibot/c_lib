#ifndef JOT_TLSF_ALLOCATOR
#define JOT_TLSF_ALLOCATOR


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
#define TLSF_ALLOC_MAX_SIZE             (isize) UINT32_MAX
#define TLSF_ALLOC_BINS                 64
#define TLSF_ALLOC_MAX_ALIGN            4096 //One page 
#define TLSF_ALLOC_INVALID              0xFFFFFFFF
#define TLSF_ALLOC_START                0
#define TLSF_ALLOC_END                  1
#define TLSF_ALLOC_CHUNK_SIZE           32
#define TLSF_ALLOC_MASK_MULTIPLE        1
#define TLSF_ALLOC_USED_BIT             ((uint8_t) 1 << 7)
#define TLSF_ALLOC_AVX2
//#define TLSF_ALLOC_SSE2

#define TLSF_ALLOC_CHECK_USED       ((uint32_t) 1 << 1)
#define TLSF_ALLOC_CHECK_FREELIST   ((uint32_t) 1 << 5)
#define TLSF_ALLOC_CHECK_DETAILED   ((uint32_t) 1 << 2)
#define TLSF_ALLOC_CHECK_ALL_NODES  ((uint32_t) 1 << 3)
#define TLSF_ALLOC_CHECK_BIN        ((uint32_t) 1 << 4)

#ifndef NDEBUG
    #define TLSF_ALLOC_DEBUG            //Enables basic safery checks on passed in nodes. Helps to find overwrites
    #define TLSF_ALLOC_DEBUG_SLOW       //Enebles extensive checks on nodes. Also fills data to garavge on alloc/free.
    #define TLSF_ALLOC_DEBUG_SLOW_SLOW  //Checks all nodes on every entry and before return of every function. Is extremely slow and should only be used when testing this allocator
#endif

typedef struct Tlsf_Alloc {
    uint32_t node;
    uint32_t offset;
    void* ptr;
} Tlsf_Alloc;

typedef struct Tlsf_Node {
    uint32_t next;  
    uint32_t prev; 
    uint32_t offset; 
    uint32_t size;
} Tlsf_Node;

#include "defines.h"

//Grug brained allocator :)

typedef struct ATTRIBUTE_ALIGNED(64) Tlsf_Mask_Set {
    uint64_t bin_masks[TLSF_ALLOC_BINS][TLSF_ALLOC_MASK_MULTIPLE];
} Tlsf_Mask_Set;

typedef struct Tlsf_Allocator {
    uint8_t* memory;
    isize memory_size;
    
    isize allocation_count;
    isize deallocation_count;
    isize bytes_allocated;
    isize max_bytes_allocated;
    isize max_concurent_allocations;

    uint32_t node_first_free;
    uint32_t node_count;
    uint32_t node_capacity;
    uint32_t mask_count;

    uint8_t* node_bins;
    Tlsf_Node* nodes;
    Tlsf_Mask_Set* masks;

    uint32_t bin_counts[TLSF_ALLOC_BINS];
    uint64_t bin_mask;
} Tlsf_Allocator;

EXPORT void       tlsf_alloc_init(Tlsf_Allocator* allocator, void* memory, isize memory_size, isize node_count);
EXPORT Tlsf_Alloc tlsf_alloc_allocate(Tlsf_Allocator* allocator, isize size, isize align);
EXPORT void       tlsf_alloc_deallocate(Tlsf_Allocator* allocator, uint32_t node_i);
EXPORT void       tlsf_alloc_reset(Tlsf_Allocator* allocator);

EXPORT uint32_t tlsf_alloc_get_node_size(Tlsf_Allocator* allocator, uint32_t node_i);
EXPORT uint32_t tlsf_alloc_get_node(Tlsf_Allocator* allocator, void* ptr);
EXPORT void*    tlsf_alloc_malloc(Tlsf_Allocator* allocator, isize size, isize align);
EXPORT void     tlsf_alloc_free(Tlsf_Allocator* allocator, void* ptr);

//Checks wheter the allocator is in valid state. If is not aborts.
// Flags can be TLSF_ALLOC_CHECK_DETAILED and TLSF_ALLOC_CHECK_ALL_NODES.
EXPORT void tlsf_alloc_test_invariants(Tlsf_Allocator* allocator, uint32_t flags);

#endif

#define JOT_ALL_IMPL

//=========================  IMPLEMENTATION BELOW ==================================================
#if (defined(JOT_ALL_IMPL) || defined(JOT_TLSF_ALLOCATOR_IMPL)) && !defined(JOT_TLSF_ALLOCATOR_HAS_IMPL)
#define JOT_TLSF_ALLOCATOR_IMPL

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
    
    inline static uint8_t _tlsf_popcount64(uint64_t num)
    {
        return (uint8_t) __popcnt64((unsigned __int64)num);
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

INTERNAL int32_t _tlsf_alloc_get_bin_floor(uint32_t size)
{
    ASSERT(size >= 0);

    //Effectively computes floor(log_beta(size/M)) where 
    // beta is the logarithm base equal to sqrt(2) and M = TLSF_ALLOC_MIN_SIZE_LOG2.
    // floor(log_beta(size)) = floor(log2(size/M)/log2(sqrt(2)) = 
    //                       = floor(log2(size/M)/0.5)
    //                       = floor(2*log2(size/M))
    //                       = floor(2*log2(size)) - 2*log2(M)
    //
    int32_t lower_bound_log2 = _tlsf_find_last_set_bit32(size);
    uint32_t lower_bound = (uint32_t) 1 << lower_bound_log2;
    uint32_t middle_point_offset = (uint32_t) 1 << (lower_bound_log2-1);

    int32_t res = 2*(int32_t)lower_bound_log2 + (int32_t) (size >= lower_bound + middle_point_offset);
    return res;
}

INTERNAL int32_t _tlsf_alloc_get_bin_ceil(uint32_t size)
{
    int32_t index = _tlsf_alloc_get_bin_floor(size);

    // Unless its power of two (thus 1 << lower_bound_log2 == size) we take the next bin!
    return index + (int32_t) !_tlsf_is_pow2_or_zero(size); 
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

EXPORT uint64_t* tlsf_alloc_get_bin_mask(Tlsf_Allocator* allocator, uint32_t bin, uint32_t chunk)
{
    ASSERT(bin < TLSF_ALLOC_BINS);
    ASSERT(chunk/64/TLSF_ALLOC_MASK_MULTIPLE < allocator->mask_count);
    return &allocator->masks[chunk/64/TLSF_ALLOC_MASK_MULTIPLE].bin_masks[bin][chunk/64%TLSF_ALLOC_MASK_MULTIPLE];
}


INTERNAL void _tlsf_alloc_check_node(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags);
INTERNAL void _tlsf_alloc_check_invariants(Tlsf_Allocator* allocator);



#include "assert.h"

INTERNAL uint32_t tlsf_find_chunk(Tlsf_Allocator* allocator, int32_t bin_i, uint64_t** out_mask)
{
    for(uint32_t i = 0; i < allocator->mask_count; i++)
    {
        uint64_t* masks = allocator->masks[i].bin_masks[bin_i];
        STATIC_ASSERT(TLSF_ALLOC_MASK_MULTIPLE == 1);
        if(masks[0]) {*out_mask = &masks[0]; return _tlsf_find_first_set_bit64(masks[0]) + 0*64 + i*TLSF_ALLOC_MASK_MULTIPLE*64;}
        //if(masks[1]) {*out_mask = &masks[1]; return _tlsf_find_first_set_bit64(masks[1]) + 1*64 + i*TLSF_ALLOC_MASK_MULTIPLE*64;}
        //if(masks[2]) {*out_mask = &masks[2]; return _tlsf_find_first_set_bit64(masks[2]) + 2*64 + i*TLSF_ALLOC_MASK_MULTIPLE*64;}
        //if(masks[3]) {*out_mask = &masks[3]; return _tlsf_find_first_set_bit64(masks[3]) + 3*64 + i*TLSF_ALLOC_MASK_MULTIPLE*64;}
    }

    return TLSF_ALLOC_INVALID;
}


INTERNAL uint64_t tlsf_chunk_scan(Tlsf_Allocator* allocator, int32_t bin_i, int32_t chunk)
{
    STATIC_ASSERT(TLSF_ALLOC_CHUNK_SIZE == 32);

    #ifdef TLSF_ALLOC_AVX2
        //If is zero we can make this op a lot cheaper by not using the set *intrinsic* (is not instruction => is series of instructions => is slower)
        __m256i looking_for = _mm256_set1_epi8((char) (uint8_t) bin_i);     
        __m256i bins = _mm256_load_si256((__m256i*) (void*) &allocator->node_bins[chunk*TLSF_ALLOC_CHUNK_SIZE]);  
        __m256i comp = _mm256_cmpeq_epi8(bins, looking_for);
        uint64_t comp_mask = (uint64_t) (uint32_t) _mm256_movemask_epi8(comp);
    #elif defined(TLSF_ALLOC_SSE2)
        __m128i looking_for = _mm_set1_epi8((char) (uint8_t) bin_i);         
        __m128i bins = _mm_load_si128((__m128i*) (void*) &allocator->node_bins[chunk*TLSF_ALLOC_CHUNK_SIZE]);     
        __m128i comp = _mm_cmpeq_epi8(bins, looking_for);
        uint64_t comp_mask = (uint64_t) (uint32_t) _mm_movemask_epi8(comp);
        if(comp_mask == 0)
        {
            bins = _mm_load_si128((__m128i*) (void*) &allocator->node_bins[chunk*TLSF_ALLOC_CHUNK_SIZE + 16]);     
            comp = _mm_cmpeq_epi8(bins, looking_for);
            comp_mask = (uint64_t) (uint32_t) _mm_movemask_epi8(comp) << 16;
        }
    #else
        #error this is going to be REALLY slow... please use SSE2 at least...
    #endif
    return comp_mask;
}

EXPORT void tlsf_alloc_mark_bin(Tlsf_Allocator* allocator, uint32_t bin, uint32_t chunk, uint64_t* mask)
{
    if(*mask == 0)
    {
        if(allocator->bin_counts[bin] == 0)
            allocator->bin_mask |= ((uint64_t) 1 << bin);
        allocator->bin_counts[bin] += 1;
    }
    *mask |= ((uint64_t) 1 << (chunk%64));
}

EXPORT void tlsf_alloc_clear_bin(Tlsf_Allocator* allocator, uint32_t bin, uint32_t chunk, uint64_t* mask)
{
    ASSERT(*mask & ((uint64_t) 1 << (chunk%64)));
    *mask &= ~((uint64_t) 1 << (chunk%64));
    if(*mask == 0)
    {
        ASSERT(allocator->bin_counts[bin] > 0);
        allocator->bin_counts[bin] -= 1;
        if(allocator->bin_counts[bin] == 0)
            allocator->bin_mask &= ~((uint64_t) 1 << bin);
    }
}

#if 0
#define TLSF_PROFILE_START(...) PERF_COUNTER_START(__VA_ARGS__)
#define TLSF_PROFILE_END(...) PERF_COUNTER_END(__VA_ARGS__)
#else
#define TLSF_PROFILE_START(...) 
#define TLSF_PROFILE_END(...) 
#endif


EXPORT Tlsf_Alloc tlsf_alloc_allocate(Tlsf_Allocator* allocator, isize size, isize align)
{
    ASSERT(allocator);
    ASSERT(size >= 0);
    ASSERT(_tlsf_is_pow2_or_zero(align) && align > 0);

    Tlsf_Alloc out = {0};
    _tlsf_alloc_check_invariants(allocator);
    ASSERT(allocator->node_count < allocator->node_capacity);
    if(size == 0 || size > 0xFFFFFFFF || allocator->node_count >= allocator->node_capacity)
        return out;

    uint32_t adjusted_size = (uint32_t) size;
    uint32_t adjusted_align = TLSF_ALLOC_MIN_SIZE;
    if(align > TLSF_ALLOC_MIN_SIZE)
    {
        adjusted_align = align < TLSF_ALLOC_MAX_ALIGN ? (uint32_t) align : TLSF_ALLOC_MAX_ALIGN;
        adjusted_size += adjusted_align;
    }
    
    TLSF_PROFILE_START(find_free_node);
    int32_t bin_from = _tlsf_alloc_get_bin_ceil(adjusted_size);
    uint64_t bins_mask = ((uint64_t) 1 << bin_from) - 1;
    uint64_t suitable_bin_mask = allocator->bin_mask & ~bins_mask;
    if(suitable_bin_mask == 0)
    {
        ASSERT(false);
        return out;
    }
        
    int32_t bin_i = _tlsf_find_first_set_bit64(suitable_bin_mask);
    uint64_t* next_bin_mask = 0;
    uint32_t next_chunk = tlsf_find_chunk(allocator, bin_i, &next_bin_mask);
    uint64_t next_chunk_mask = tlsf_chunk_scan(allocator, bin_i, next_chunk);
    uint32_t next_offset = _tlsf_find_first_set_bit64(next_chunk_mask);
    TLSF_PROFILE_END(find_free_node);
    
    TLSF_PROFILE_START(update_node);
    uint32_t node_i = allocator->node_first_free;
    uint32_t next_i = TLSF_ALLOC_CHUNK_SIZE*next_chunk + next_offset;
    
    _tlsf_alloc_check_node(allocator, node_i, TLSF_ALLOC_CHECK_FREELIST);

    Tlsf_Node* __restrict next = &allocator->nodes[next_i];
    Tlsf_Node* __restrict prev = &allocator->nodes[next->prev];
    Tlsf_Node* __restrict node = &allocator->nodes[node_i];
    
    uint32_t prev_i = next->prev;
    _tlsf_alloc_check_node(allocator, prev_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_node(allocator, next_i, TLSF_ALLOC_CHECK_USED);

    uint8_t* next_bin = &allocator->node_bins[next_i];
    uint8_t* node_bin = &allocator->node_bins[node_i];
    ASSERT(*node_bin == 0xFF);
    *node_bin = 0xF0;

    allocator->node_first_free = node->next;
    allocator->node_count += 1;

    node->offset = prev->offset + prev->size;
    node->size = adjusted_size;
    node->next = next_i;
    node->prev = prev_i;

    prev->next = node_i;
    next->prev = node_i;
    TLSF_PROFILE_END(update_node);
 
    ASSERT(next->offset >= node->offset + node->size);
    uint32_t new_next_portion = next->offset - (node->offset + node->size);

    uint8_t old_next_bin = *next_bin;
    uint8_t new_next_bin = 0xF0;
    //if(new_next_portion >= 8)
    //if(new_next_bin != old_next_bin)
    {
        TLSF_PROFILE_START(reassign);
        if(_tlsf_is_pow2_or_zero(next_chunk_mask) && old_next_bin != 0xF0)
            tlsf_alloc_clear_bin(allocator, old_next_bin, next_chunk, next_bin_mask);

        
        //if(new_next_bin != 0xF0)
        if(new_next_portion >= 8)
        {
            new_next_bin = (uint8_t) _tlsf_alloc_get_bin_floor(new_next_portion);
            uint64_t* next_new_bin_mask = tlsf_alloc_get_bin_mask(allocator, new_next_bin, next_chunk);
            tlsf_alloc_mark_bin(allocator, new_next_bin, next_chunk, next_new_bin_mask);
        }
        *next_bin = new_next_bin;
        TLSF_PROFILE_END(reassign);
    }
    
    #if 0
    TLSF_PROFILE_START(update_stats);
    allocator->allocation_count += 1;
    if(allocator->max_concurent_allocations < allocator->allocation_count - allocator->deallocation_count)
        allocator->max_concurent_allocations = allocator->allocation_count - allocator->deallocation_count;

    allocator->bytes_allocated += adjusted_size;
    if(allocator->max_bytes_allocated < allocator->bytes_allocated)
        allocator->max_bytes_allocated = allocator->bytes_allocated;
    TLSF_PROFILE_END(update_stats);
    #endif
        
    out.node = node_i;
    out.offset = node->offset;
    if(allocator->memory)
        out.ptr = allocator->memory + node->offset;

    _tlsf_alloc_check_node(allocator, node_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_node(allocator, prev_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_node(allocator, next_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_invariants(allocator);
        
    return out;
}

EXPORT void tlsf_alloc_deallocate(Tlsf_Allocator* allocator, uint32_t node_i)
{
    ASSERT(allocator);
    _tlsf_alloc_check_invariants(allocator);
    if(node_i == 0)
        return;
        
    _tlsf_alloc_check_node(allocator, node_i, TLSF_ALLOC_CHECK_USED);
    Tlsf_Node* __restrict node = &allocator->nodes[node_i]; 

    uint32_t next_i = node->next;
    uint32_t prev_i = node->prev;

    _tlsf_alloc_check_node(allocator, next_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_node(allocator, prev_i, TLSF_ALLOC_CHECK_USED);

    Tlsf_Node* __restrict next = &allocator->nodes[next_i];
    Tlsf_Node* __restrict prev = &allocator->nodes[prev_i];
    
    uint8_t* node_bin = &allocator->node_bins[node_i];
    uint8_t* next_bin = &allocator->node_bins[next_i];
    
    //Clear the nodes bin
    if(*node_bin != 0xF0)
    {
        TLSF_PROFILE_START(unlink);
        uint32_t node_chunk = node_i/TLSF_ALLOC_CHUNK_SIZE;
        uint64_t* node_bin_mask = tlsf_alloc_get_bin_mask(allocator, *node_bin, node_chunk);
        uint64_t node_chunk_mask = tlsf_chunk_scan(allocator, *node_bin, node_chunk);

        ASSERT(node_chunk_mask > 0);
        if(_tlsf_is_pow2_or_zero(node_chunk_mask))
            tlsf_alloc_clear_bin(allocator, *node_bin, node_chunk, node_bin_mask);
        TLSF_PROFILE_END(unlink);
    }
    *node_bin = 0xFF;
    
    next->prev = prev_i;
    prev->next = next_i;

    ASSERT(next->offset > prev->offset + prev->size);
    uint32_t next_portion = next->offset - (prev->offset + prev->size);
    uint8_t new_next_bin = (uint8_t) _tlsf_alloc_get_bin_floor(next_portion); 
    uint8_t old_next_bin = *next_bin;
    //If the increased portion of the next node results in different bin
    // remove from its bin and reassign.
    //if(new_next_bin != old_next_bin)
    {
        TLSF_PROFILE_START(reassign);
        uint32_t next_chunk = next_i/TLSF_ALLOC_CHUNK_SIZE;
        if(old_next_bin != 0xF0)
        {
            uint64_t* next_bin_mask = tlsf_alloc_get_bin_mask(allocator, old_next_bin, next_chunk);
            uint64_t next_chunk_mask = tlsf_chunk_scan(allocator, old_next_bin, next_chunk);
        
            ASSERT(next_chunk_mask > 0);
            if(_tlsf_is_pow2_or_zero(next_chunk_mask))
                tlsf_alloc_clear_bin(allocator, old_next_bin, next_chunk, next_bin_mask);
        }

        uint64_t* next_new_bin_mask = tlsf_alloc_get_bin_mask(allocator, new_next_bin, next_chunk);
        tlsf_alloc_mark_bin(allocator, new_next_bin, next_chunk, next_new_bin_mask);
        *next_bin = new_next_bin;
        _tlsf_alloc_check_node(allocator, next_i, TLSF_ALLOC_CHECK_USED);
        TLSF_PROFILE_END(reassign);
    }
    

    node->next = allocator->node_first_free;
    allocator->node_first_free = node_i;
    allocator->node_count -= 1;

    #if 0
        ASSERT(allocator->bytes_allocated >= node->size);
        allocator->bytes_allocated -= node->size;
        allocator->deallocation_count += 1;
    #endif
    #ifdef TLSF_ALLOC_DEBUG
        node->prev = TLSF_ALLOC_INVALID;
        node->size = TLSF_ALLOC_INVALID;
        node->offset = TLSF_ALLOC_INVALID;
    #endif
    
    _tlsf_alloc_check_node(allocator, next_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_node(allocator, prev_i, TLSF_ALLOC_CHECK_USED);
    _tlsf_alloc_check_node(allocator, node_i, TLSF_ALLOC_CHECK_FREELIST);
    _tlsf_alloc_check_invariants(allocator);
}

EXPORT void tlsf_alloc_init(Tlsf_Allocator* allocator, void* memory, isize memory_size, isize request_node_capacity)
{
    ASSERT(allocator);
    ASSERT(memory_size >= 0);
    memset(allocator, 0, sizeof *allocator);   

    //Size for START and END nodes
    isize mask_count = DIV_CEIL(request_node_capacity + 2, TLSF_ALLOC_CHUNK_SIZE*64*TLSF_ALLOC_MASK_MULTIPLE);
    isize node_count = mask_count*TLSF_ALLOC_CHUNK_SIZE*64*TLSF_ALLOC_MASK_MULTIPLE;

    allocator->nodes = (Tlsf_Node*) align_forward(malloc(node_count*sizeof(Tlsf_Node) + 64), 64);
    allocator->node_bins = (uint8_t*) align_forward(malloc(node_count*sizeof(uint8_t) + 64), 64);
    allocator->masks = (Tlsf_Mask_Set*) align_forward(malloc(mask_count*sizeof(Tlsf_Mask_Set) + 64), 64);
    if(allocator->nodes == NULL || allocator->node_bins == NULL || allocator->masks == NULL)
        return;

    allocator->memory = (uint8_t*) memory;
    allocator->memory_size = (uint32_t) memory_size;
    allocator->mask_count = (uint32_t) mask_count;
    allocator->node_capacity = (uint32_t)node_count;
    allocator->node_count = 0;

    memset(allocator->masks, 0, (size_t) mask_count*sizeof(Tlsf_Mask_Set));
    memset(allocator->node_bins, 0xFF, (size_t) node_count*sizeof(uint8_t));

    allocator->node_first_free = TLSF_ALLOC_INVALID;
    for(uint32_t i = allocator->node_capacity; i-- > 0;)
    {
        Tlsf_Node* node = &allocator->nodes[i];
        node->next = allocator->node_first_free; 
        node->offset = TLSF_ALLOC_INVALID;
        node->prev = TLSF_ALLOC_INVALID;
        node->size = TLSF_ALLOC_INVALID;

        allocator->node_first_free = i;
    }

    //Push START and END nodes
    uint32_t start_i = allocator->node_first_free;
    Tlsf_Node* start = &allocator->nodes[start_i]; 
    allocator->node_first_free = start->next;
    allocator->node_count += 1;
    
    uint32_t end_i = allocator->node_first_free;
    Tlsf_Node* end = &allocator->nodes[end_i]; 
    allocator->node_first_free = end->next;
    allocator->node_count += 1;

    ASSERT(start_i == TLSF_ALLOC_START);
    ASSERT(end_i == TLSF_ALLOC_END);

    start->prev = TLSF_ALLOC_INVALID;
    start->next = TLSF_ALLOC_END;
    start->size = 0;
    start->offset = 0;
    
    end->prev = TLSF_ALLOC_START;
    end->next = TLSF_ALLOC_INVALID;
    end->size = 0;
    end->offset = (uint32_t) memory_size;

    uint32_t end_chunk = TLSF_ALLOC_END/TLSF_ALLOC_CHUNK_SIZE; // = 0
    uint8_t end_bin = (uint8_t) _tlsf_alloc_get_bin_floor((uint32_t) memory_size); 
    uint64_t* end_chunk_mask = tlsf_alloc_get_bin_mask(allocator, end_bin, end_chunk);
    tlsf_alloc_mark_bin(allocator, end_bin, end_chunk, end_chunk_mask);
    
    allocator->node_bins[TLSF_ALLOC_START] = 0xF0;
    allocator->node_bins[TLSF_ALLOC_END] = end_bin;
    
    _tlsf_alloc_check_invariants(allocator);
}


INTERNAL void tlsf_alloc_test_node_invariants(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags, uint32_t bin_i)
{
    TEST(0 <= node_i && node_i < allocator->node_capacity);
    //TEST(node_i != TLSF_ALLOC_START && node_i != TLSF_ALLOC_END, "Must not be START or END node!");
    Tlsf_Node* node = &allocator->nodes[node_i];
    uint8_t* node_bin = &allocator->node_bins[node_i];
    
    bool node_is_free = !!(*node_bin == 0xFF);
    if(flags & TLSF_ALLOC_CHECK_BIN)
        TEST(*node_bin == bin_i);
    if(flags & TLSF_ALLOC_CHECK_USED)
        TEST(node_is_free == false);
    if(flags & TLSF_ALLOC_CHECK_FREELIST)
        TEST(node_is_free);

    if(node_is_free)
    {
        #ifdef TLSF_ALLOC_DEBUG
            TEST(node->offset == TLSF_ALLOC_INVALID);
            TEST(node->prev == TLSF_ALLOC_INVALID);
            TEST(node->size == TLSF_ALLOC_INVALID);
        #endif
    }
    else
    {
        TEST(node->offset <= allocator->memory_size);
        TEST(node->prev < allocator->node_capacity || node_i == TLSF_ALLOC_START);
        TEST(node->next < allocator->node_capacity || node_i == TLSF_ALLOC_END);
        TEST(node->size > 0 || node_i == TLSF_ALLOC_START || node_i == TLSF_ALLOC_END);

        if((flags & TLSF_ALLOC_CHECK_DETAILED) && node_i != TLSF_ALLOC_END)
        {
            Tlsf_Node* next = &allocator->nodes[node->next];
            TEST(next->prev == node_i);
            TEST(node->offset <= next->offset);
        }
        
        if((flags & TLSF_ALLOC_CHECK_DETAILED) && node_i != TLSF_ALLOC_START)
        {
            Tlsf_Node* prev = &allocator->nodes[node->prev];
            TEST(node->next != node_i);
            TEST(node->prev != node_i);
            TEST(prev->next == node_i);
            TEST(prev->offset <= node->offset);

            uint32_t node_portion = node->offset - (prev->offset + prev->size);
            
            TEST((*node_bin == 0xF0) == (node_portion == 0));
            if(*node_bin != 0xF0)
            {
                uint32_t calculated_bin = _tlsf_alloc_get_bin_floor(node_portion);
                TEST(*node_bin == calculated_bin);

                uint32_t chunk = node_i/TLSF_ALLOC_CHUNK_SIZE;
                uint64_t* bin_mask = tlsf_alloc_get_bin_mask(allocator, *node_bin, chunk);
                TEST(*bin_mask & (uint64_t) 1 << chunk%64);
            }
        }
    }
}

EXPORT void tlsf_alloc_test_invariants(Tlsf_Allocator* allocator, uint32_t flags)
{
    //Check fields
    TEST((allocator->nodes == NULL) == (allocator->node_capacity == 0));
    TEST(allocator->node_count <= allocator->node_capacity);

    TEST(allocator->allocation_count >= allocator->deallocation_count);
    TEST(allocator->allocation_count - allocator->deallocation_count <= allocator->max_concurent_allocations);
    TEST(allocator->bytes_allocated <= allocator->max_bytes_allocated);
    
    //Check START and END nodes.
    Tlsf_Node* start = &allocator->nodes[TLSF_ALLOC_START];
    TEST(start->prev == TLSF_ALLOC_INVALID);
    TEST(start->offset == 0);
    TEST(start->size == 0);
    TEST(allocator->node_bins[TLSF_ALLOC_START] == 0xF0);
    
    Tlsf_Node* end = &allocator->nodes[TLSF_ALLOC_END];
    TEST(end->next == TLSF_ALLOC_INVALID);
    TEST(end->offset == (uint32_t) allocator->memory_size);
    TEST(end->size == 0);
        
    if(flags & TLSF_ALLOC_CHECK_ALL_NODES)
    {
        //Check if bin free lists match the mask
        for(int32_t i = 0; i < TLSF_ALLOC_BINS; i++)
        {
            bool has_ith_bin = allocator->bin_counts[i] != 0;
            uint64_t ith_bit = (uint64_t) 1 << i;
            TEST(!!(allocator->bin_mask & ith_bit) == has_ith_bin);
        }

        //Check free list
        uint32_t nodes_in_free_list = 0;
        for(uint32_t node_i = allocator->node_first_free; node_i != TLSF_ALLOC_INVALID; nodes_in_free_list++)
        {
            TEST(nodes_in_free_list <= allocator->node_capacity - allocator->node_count);
            uint8_t* node_bin = &allocator->node_bins[node_i];
            TEST(*node_bin == 0xFF);
            Tlsf_Node* node = &allocator->nodes[node_i];
            node_i = node->next;
        }

        //Go through all nodes in all bins and count them. 
        for(int32_t bin_i = 0; bin_i < TLSF_ALLOC_BINS; bin_i++)
        {
            uint32_t nonempty_masks = 0;
            for(uint32_t i = 0; i < allocator->mask_count*TLSF_ALLOC_MASK_MULTIPLE; i++)
            {
                uint32_t mask_set = i/TLSF_ALLOC_MASK_MULTIPLE;
                uint32_t mask_mult = i%TLSF_ALLOC_MASK_MULTIPLE;
                uint64_t* bin_mask = &allocator->masks[mask_set].bin_masks[bin_i][mask_mult]; 
                if(*bin_mask == 0)
                    continue;

                uint32_t offset = _tlsf_find_first_set_bit64(*bin_mask);
                uint32_t chunk_i = i*64 + offset;
                
                //Ensure that if a bin is marked there is a node node with the matching bin
                uint64_t chunk_mask = tlsf_chunk_scan(allocator, bin_i, chunk_i);
                TEST(chunk_mask != 0);
                nonempty_masks += 1;
            }
            
            TEST(nonempty_masks == allocator->bin_counts[bin_i]);
            int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
        }

        //Go through all nodes in order and count them.
        //Ensure the list is not cyclical
        uint32_t nodes_in_use = 0;
        for(uint32_t node_i = TLSF_ALLOC_START; node_i != TLSF_ALLOC_INVALID; nodes_in_use++)
        {
            TEST(nodes_in_use < allocator->node_capacity);
            Tlsf_Node* node = &allocator->nodes[node_i];
            node_i = node->next;
        }

        //Go through all the nodes and count them. Check their status
        uint32_t nodes_in_use2 = 0;
        uint32_t nodes_in_free_list2 = 0;
        for(uint32_t node_i = 0; node_i < allocator->node_capacity; node_i++)
        {
            uint8_t* node_bin = &allocator->node_bins[node_i];
            //Tlsf_Node* node = &allocator->nodes[node_i];

            if(*node_bin == 0xFF)
                nodes_in_free_list2 += 1;
            else
                nodes_in_use2 += 1;

            tlsf_alloc_test_node_invariants(allocator, node_i, flags, 0);
        }

        TEST(allocator->node_count == nodes_in_use);
        TEST(allocator->node_capacity == nodes_in_free_list + nodes_in_use);
        TEST(nodes_in_use == nodes_in_use2);
        TEST(nodes_in_free_list == nodes_in_free_list2);
        int vs_debugger_stupid = 0; (void) vs_debugger_stupid;
    }
}


INTERNAL void _tlsf_alloc_check_node(Tlsf_Allocator* allocator, uint32_t node_i, uint32_t flags)
{
    (void) allocator;
    (void) node_i;
    (void) flags;
    #ifdef TLSF_ALLOC_DEBUG
        #ifdef TLSF_ALLOC_DEBUG_SLOW
            flags |= TLSF_ALLOC_CHECK_DETAILED;
        #else
            flags &= ~TLSF_ALLOC_CHECK_DETAILED;
        #endif

        tlsf_alloc_test_node_invariants(allocator, node_i, flags, 0);
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

        tlsf_alloc_test_invariants(allocator, flags);
    #endif
}

EXPORT void tlsf_alloc_reset(Tlsf_Allocator* allocator)
{
    //TODO
    tlsf_alloc_init(allocator, allocator->memory, allocator->memory_size, 0);
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

    Tlsf_Allocator allocator = {0};
    tlsf_alloc_init(&allocator, NULL, memory_size, 1024);

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
        tlsf_alloc_test_invariants(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);
        allocs[i].node = tlsf_alloc_allocate(&allocator, allocs[i].size, allocs[i].align).node;
        tlsf_alloc_test_invariants(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);
    }
        
    for(isize i = 0; i < STATIC_ARRAY_SIZE(allocs); i++)
    {
        tlsf_alloc_test_invariants(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);
        tlsf_alloc_deallocate(&allocator, allocs[i].node);
        tlsf_alloc_test_invariants(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);
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
        MAX_AT_ONCE = 1024,
    };
    const double MAX_PERTURBATION = 0.2;
    
    ASSERT(at_once < MAX_AT_ONCE);
    isize memory_size = 1024*1024*1024;

    Tlsf_Allocator allocator = {0};
    tlsf_alloc_init(&allocator, NULL, memory_size, MAX_AT_ONCE);
    
    struct {
        uint32_t size;
        uint32_t align;
        uint32_t node;
    } allocs[MAX_AT_ONCE] = {0};

    isize iter = 0;
    for(double start = _tlsf_clock_s(); _tlsf_clock_s() - start < seconds;)
    {
        isize i = _tlsf_random_range(0, at_once);
        if(iter < at_once)
            i = iter;
        else
        {
            tlsf_alloc_deallocate(&allocator, allocs[i].node);
            tlsf_alloc_test_invariants(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);
        }
        
        double perturbation = 1 + _tlsf_random_interval(-MAX_PERTURBATION, MAX_PERTURBATION);
        isize random_align_shift = _tlsf_random_range(0, MAX_ALIGN_LOG2);
        isize random_size_shift = _tlsf_random_range(0, MAX_SIZE_LOG2);

        //Random exponentially distributed sizes with small perturbances.
        allocs[i].size = (int32_t)(((isize) 1 << random_size_shift) * perturbation);
        allocs[i].align = (int32_t) ((isize) 1 << random_align_shift);
        allocs[i].node = tlsf_alloc_allocate(&allocator, allocs[i].size, allocs[i].align).node;
        
        tlsf_alloc_test_invariants(&allocator, TLSF_ALLOC_CHECK_DETAILED | TLSF_ALLOC_CHECK_ALL_NODES);

        iter += 1;
    }
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
    void benchmark_tlsf_alloc_single(double seconds, bool touch, isize at_once, isize min_size, isize max_size, isize min_align_log2, isize max_align_log2)
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
        tlsf_alloc_init(&tlsf, tlsf_memory, memory_size, at_once*1000);

        Perf_Stats stats_tlsf_alloc = {0};
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
                stats_alloc = &stats_tlsf_alloc;
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
                    
                _tlsf_alloc_check_invariants(&tlsf);

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
                        tlsf_alloc_deallocate(&tlsf, allocs[i].node);
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
                    Tlsf_Alloc alloc = tlsf_alloc_allocate(&tlsf, random.size, random.align);
                    allocs[i].node = alloc.node;
                    allocs[i].ptr = alloc.ptr;
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
        arena_deinit(&arena);

        log_perf_stats_hdr("BENCH", LOG_INFO, "ALLOC:        ");
        log_perf_stats_row("BENCH", LOG_INFO, "arena         ", stats_arena_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf          ", stats_tlsf_alloc);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc        ", stats_malloc_alloc);
        
        log_perf_stats_hdr("BENCH", LOG_INFO, "FREE:         ");
        log_perf_stats_row("BENCH", LOG_INFO, "arena         ", stats_arena_free);
        log_perf_stats_row("BENCH", LOG_INFO, "tlsf          ", stats_tlsf_free);
        log_perf_stats_row("BENCH", LOG_INFO, "malloc        ", stats_malloc_free);
    }

    void benchmark_tlsf_alloc(bool touch, double seconds)
    {
        benchmark_tlsf_alloc_single(seconds, touch, 4*4096, 8, 64, 0, 4);
        benchmark_tlsf_alloc_single(seconds, touch, 4096, 8, 64, 0, 4);
        benchmark_tlsf_alloc_single(seconds, touch, 1024, 64, 512, 0, 4);
        //benchmark_tlsf_alloc_single(seconds, touch, 1024, 8, 64, 0, 4);
        //benchmark_tlsf_alloc_single(seconds, touch, 256, 64, 512, 0, 4);
        //benchmark_tlsf_alloc_single(seconds, touch, 1024, 4000, 8000, 0, 4);
    }
    #endif
#endif