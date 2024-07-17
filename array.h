#ifndef JOT_ARRAY
#define JOT_ARRAY

// This freestanding file introduces a simple but powerful typed dynamic array concept.
// It works by defining struct for each type and then using type untyped macros to work
// with these structs. 
//
// This approach was chosen because: 
// 1) we need type safety! Array of int should be distinct type from Array of char
//    This disqualified the one Array struct for all types holding the type info supplied at runtime.
// 
// 2) we need to be able to work with empty arrays easily and safely. 
//    Empty arrays are the most common arrays so having them as a special and error prone case
//    is less than ideal. This disqualified the typed pointer to allocated array prefixed with header holding
//    the meta data. See how stb library implements "stretchy buffers".
//    This approach also introduces a lot of ifs, makes the meta data address unstable thus requiring more memory lookups.
// 
// 3) we need to hold info about allocators used for the array. We should know how to deallocate any array using its allocator.
//
// 4) the array type must be fully explicit. There should never be the case where we return an array from a function and we dont know
//    what kind of array it is/if it even is a dynamic array. This is another issue with the stb style.
//
// This file is also fully freestanding. To compile the function definitions #define JOT_ALL_IMPL and include it again in .c file. 

#if !defined(JOT_INLINE_ALLOCATOR) && !defined(JOT_ALLOCATOR) && !defined(JOT_COUPLED)
    #define JOT_INLINE_ALLOCATOR
    #include <stdint.h>
    #include <stdlib.h>
    #include <assert.h>
    #include <stdbool.h>
    
    #define EXTERNAL
    #define INTERNAL static
    #define DEF_ALIGN sizeof(void*)
    #define ASSERT(x) assert(x)

    typedef int64_t isize; //can also be usnigned if desired
    typedef struct Allocator Allocator;
    
    static Allocator* allocator_get_default() 
    { 
        return NULL; 
    }
    static void* allocator_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        if(new_size != 0)
            return realloc(old_ptr, new_size);
        else
        {
            free(old_ptr);
            return NULL;
        }
    }
#else
    #include "allocator.h"
#endif

typedef struct Untyped_Array {        
    Allocator* allocator;                
    uint8_t* data;                      
    isize len;                        
    isize capacity;                    
} Untyped_Array;

typedef struct Generic_Array {   
    Untyped_Array* array;
    uint32_t item_size;                        
    uint32_t item_align;                    
} Generic_Array;

#define Array_Aligned(Type, align)               \
    union {                                      \
        Untyped_Array untyped;                   \
        struct {                                 \
            Allocator* allocator;                \
            Type* data;                          \
            isize len;                          \
            isize capacity;                      \
        };                                       \
        uint8_t (*ALIGN)[align];                 \
    }                                            \

#define Array(Type) Array_Aligned(Type, __alignof(Type) > 0 ? __alignof(Type) : 8)

typedef Array(uint8_t)  u8_Array;
typedef Array(uint16_t) u16_Array;
typedef Array(uint32_t) u32_Array;
typedef Array(uint64_t) u64_Array;

typedef Array(int8_t)   i8_Array;
typedef Array(int16_t)  i16_Array;
typedef Array(int32_t)  i32_Array;
typedef Array(int64_t)  i64_Array;

typedef Array(float)    f32_Array;
typedef Array(double)   f64_Array;
typedef Array(void*)    ptr_Array;

typedef i64_Array isize_Array;
typedef u64_Array usize_Array;

EXTERNAL void generic_array_init(Generic_Array gen, Allocator* allocator);
EXTERNAL void generic_array_deinit(Generic_Array gen);
EXTERNAL void generic_array_set_capacity(Generic_Array gen, isize capacity); 
EXTERNAL bool generic_array_is_invariant(Generic_Array gen);
EXTERNAL void generic_array_resize(Generic_Array gen, isize to_size, bool zero_new);
EXTERNAL void generic_array_reserve(Generic_Array gen, isize to_capacity);
EXTERNAL void generic_array_append(Generic_Array gen, const void* data, isize data_count);

#ifdef __cplusplus
    #define array_make_generic(array_ptr) (Generic_Array{&(array_ptr)->untyped, sizeof *(array_ptr)->data, sizeof *(array_ptr)->ALIGN})
#else
    #define array_make_generic(array_ptr) ((Generic_Array){&(array_ptr)->untyped, sizeof *(array_ptr)->data, sizeof *(array_ptr)->ALIGN})
#endif 

#define array_set_capacity(array_ptr, capacity) \
    generic_array_set_capacity(array_make_generic(array_ptr), (capacity))
    
//Initializes the array. If the array is already initialized deinitializes it first.
//Thus expects a properly formed array. Suppling a non-zeroed memory will cause errors!
//All data structers in this library need to be zero init to be valid!
#define array_init(array_ptr, allocator) \
    generic_array_init(array_make_generic(array_ptr), (allocator))
    
//Initializes the array and preallocates it to the desired size
#define array_init_with_capacity(array_ptr, allocator, capacity) (\
        array_init((array_ptr), (allocator)), \
        array_reserve((array_ptr), (capacity)) \
    )

//Deallocates and resets the array
#define array_deinit(array_ptr) \
    generic_array_deinit(array_make_generic(array_ptr))

//If the array capacity is lower than to_capacity sets the capacity to to_capacity. 
//If setting of capacity is required and the new capcity is less then one geometric growth 
// step away from current capacity grows instead.
#define array_reserve(array_ptr, to_capacity) \
    generic_array_reserve(array_make_generic(array_ptr), (to_capacity)) 

//Sets the array size to the specied to_size. 
//If the to_size is smaller than current size simply dicards further items
//If the to_size is greater than current size zero initializes the newly added items
#define array_resize(array_ptr, to_size)              \
    generic_array_resize(array_make_generic(array_ptr), (to_size), true) 
   
//Just like array_resize except doesnt zero initialized newly added region
#define array_resize_for_overwrite(array_ptr, to_size)              \
    generic_array_resize(array_make_generic(array_ptr), (to_size), false) 

//Sets the array size to 0. Does not deallocate the array
#define array_clear(array_ptr) \
    array_resize_for_overwrite((array_ptr), 0)

//Appends item_count items to the end of the array growing it
#define array_append(array_ptr, items, item_count) (\
        /* Here is a little hack to typecheck the items array.*/ \
        /* We do a comparison that emmits a warning on incompatible types but doesnt get executed */ \
        sizeof((array_ptr)->data == (items)), \
        generic_array_append(array_make_generic(array_ptr), (items), (item_count)) \
    ) \
    
//Discards current items in the array and replaces them with the provided items
#define array_assign(array_ptr, items, item_count) \
    array_clear(array_ptr), \
    array_append((array_ptr), (items), (item_count))
    
//Copies from from_arr into to_arr_ptr overriding its elements. 
#define array_copy(to_arr_ptr, from_arr) \
    array_assign((to_arr_ptr), (from_arr).data, (from_arr).len)

//Appends a single item to the end of the array
#define array_push(array_ptr, item_value) (           \
        generic_array_reserve(array_make_generic(array_ptr), (array_ptr)->len + 1), \
        (array_ptr)->data[(array_ptr)->len++] = (item_value) \
    ) \

//Removes a single item from the end of the array
#define array_pop(array_ptr) (\
        ASSERT((array_ptr)->len > 0 && "cannot pop from empty array!"), \
        (array_ptr)->data[--(array_ptr)->len] \
    ) \
    
//Returns the value of the last item. The array must not be empty!
#define array_last(array) (\
        ASSERT((array).len > 0 && "cannot get last from empty array!"), \
        &(array).data[(array).len - 1] \
    ) \

//Returns the total size of the array in bytes
#define array_byte_size(array) \
    ((array).len * isizeof *(array).data)

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARRAY_IMPL)) && !defined(JOT_ARRAY_HAS_IMPL)
#define JOT_ARRAY_HAS_IMPL
#include <string.h>

EXTERNAL bool generic_array_is_invariant(Generic_Array gen)
{
    bool is_capacity_correct = 0 <= gen.array->capacity;
    bool is_size_correct = (0 <= gen.array->len && gen.array->len <= gen.array->capacity);
    #ifndef JOT_INLINE_ALLOCATOR
    if(gen.array->capacity > 0)
        is_capacity_correct = is_capacity_correct && gen.array->allocator != NULL;
    #endif

    bool is_data_correct = (gen.array->data == NULL) == (gen.array->capacity == 0);
    bool item_size_correct = gen.item_size > 0;
    bool alignment_correct = ((gen.item_align & (gen.item_align-1)) == 0) && gen.item_align > 0; //if is power of two and bigger than zero
    bool result = is_capacity_correct && is_size_correct && is_data_correct && item_size_correct && alignment_correct;
    ASSERT(result);
    return result;
}

EXTERNAL void generic_array_init(Generic_Array gen, Allocator* allocator)
{
    generic_array_deinit(gen);

    gen.array->allocator = allocator;
    if(gen.array->allocator == NULL)
        gen.array->allocator = allocator_get_default();
}

EXTERNAL void generic_array_deinit(Generic_Array gen)
{
    ASSERT(gen.array != NULL);
    ASSERT(generic_array_is_invariant(gen));
    if(gen.array->capacity > 0)
        allocator_reallocate(gen.array->allocator, 0, gen.array->data, gen.array->capacity * gen.item_size, DEF_ALIGN);
    
    memset(gen.array, 0, sizeof *gen.array);
}

EXTERNAL void generic_array_set_capacity(Generic_Array gen, isize capacity)
{
    ASSERT(generic_array_is_invariant(gen));
    ASSERT(capacity >= 0);

    isize old_byte_size = gen.item_size * gen.array->capacity;
    isize new_byte_size = gen.item_size * capacity;
    if(gen.array->allocator == NULL)
        gen.array->allocator = allocator_get_default();

    gen.array->data = (uint8_t*) allocator_reallocate(gen.array->allocator, new_byte_size, gen.array->data, old_byte_size, DEF_ALIGN);

    //trim the size if too big
    gen.array->capacity = capacity;
    if(gen.array->len > gen.array->capacity)
        gen.array->len = gen.array->capacity;
        
    ASSERT(generic_array_is_invariant(gen));
}

EXTERNAL void generic_array_resize(Generic_Array gen, isize to_size, bool zero_new)
{
    generic_array_reserve(gen, to_size);
    if(zero_new && to_size > gen.array->len)
        memset(gen.array->data + gen.array->len*gen.item_size, 0, (size_t) ((to_size - gen.array->len)*gen.item_size));
        
    gen.array->len = to_size;
    ASSERT(generic_array_is_invariant(gen));
}

EXTERNAL void generic_array_reserve(Generic_Array gen, isize to_fit)
{
    ASSERT(generic_array_is_invariant(gen));
    ASSERT(to_fit >= 0);
    if(gen.array->capacity > to_fit)
        return;
        
    isize new_capacity = to_fit;
    isize growth_step = gen.array->capacity * 3/2 + 8;
    if(new_capacity < growth_step)
        new_capacity = growth_step;

    generic_array_set_capacity(gen, new_capacity + 1);
}

EXTERNAL void generic_array_append(Generic_Array gen, const void* data, isize data_count)
{
    ASSERT(data_count >= 0);
    generic_array_reserve(gen, gen.array->len+data_count);
    memcpy(gen.array->data + gen.item_size * gen.array->len, data, (size_t) (gen.item_size * data_count));
    gen.array->len += data_count;
    ASSERT(generic_array_is_invariant(gen));
}
#endif
