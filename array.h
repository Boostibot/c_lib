#ifndef JOT_ARRAY
#define JOT_ARRAY

// This file introduces a simple but powerful generic dynamic array concept.
// It works by defining struct for each type and then using type generic macros to work
// with these structs. 
//
// This approach was chosen because: 
// 1) we need type safety! Array of int should be distinct type from Array of char
//    This discvalified the one Array struct for all types holding the size of the type currently used)
// 
// 2) we need to be able to work with empty arrays easily and safely. 
//    Empty arrays are the most common arrays there are so having them as a special and error prone case
//    is less than ideal. This discvalifies the typed pointer to allocated array prefixed with header holding
//    the meta data. (See how stb library implements "stretchy buffers" and images).
//    This approach introduces a lot of ifs, makes the emta data adress unstable and requires more memory lookups.
// 
// 3) we need to hold info about allocators used for the array.
//    It also must be fully explicit ie there should never be the case where we return an array from a function and we dont know
//    how to deallocate it. This is another reason why the typed approach from 2) is bad - we dont even know we need to allocate it!
//
// Additionally these arrays implemnt the following:
// 1) EXPERIMENTAL: All arrays are null terminated with null termination of the size of the element. 
//    This is done so that we dont need a separate structure for strings but might incur too much overhead - we will see about this. 
// 
// 2) Because the functions are implemented as macros we make them all Source_Info transparent. We track locations of all allocations
//    for debugging purposes. Normally we use Source_Info fromn the allocation site - here however we use Source_Info from the use site of the
//    array function.

#include "defines.h"
#include "allocator.h"

//@Note: We can supply aligment to this 

#define DEFINE_ARRAY_TYPE(Type, Struct_Name) \
    typedef struct Struct_Name {             \
        Allocator* allocator;                \
        union {                              \
            Type* data;                      \
            Type* _array_data; /* used inside macros so that we cannot accidentally use for example String or String_Builder as an array. */ \
            uint8_t (*_alignment)[DEF_ALIGN]; /* can be used to querry alignement. Currently unused */ \
        };                                   \
        int64_t size;                        \
        int64_t capacity;                    \
        int64_t marker;                      \
    } Struct_Name                            \

DEFINE_ARRAY_TYPE(uint8_t,  u8_Array);
DEFINE_ARRAY_TYPE(uint16_t, u16_Array);
DEFINE_ARRAY_TYPE(uint32_t, u32_Array);
DEFINE_ARRAY_TYPE(uint64_t, u64_Array);

DEFINE_ARRAY_TYPE(int8_t,   i8_Array);
DEFINE_ARRAY_TYPE(int16_t,  i16_Array);
DEFINE_ARRAY_TYPE(int32_t,  i32_Array);
DEFINE_ARRAY_TYPE(int64_t,  i64_Array);

DEFINE_ARRAY_TYPE(float,    f32_Array);
DEFINE_ARRAY_TYPE(double,   f64_Array);
DEFINE_ARRAY_TYPE(void*,    ptr_Array);

typedef i64_Array isize_Array;
typedef u64_Array usize_Array;

EXPORT void _array_init(void* array, isize item_size, Allocator* allocator, Source_Info from);
EXPORT void _array_deinit(void* array, isize item_size, Source_Info from);
EXPORT void _array_set_capacity(void* array, isize item_size, isize capacity, Source_Info from); 
EXPORT bool _array_is_invariant(const void* array, isize item_size);
EXPORT void _array_resize(void* array, isize item_size, isize to_size, bool zero_new, Source_Info from);
EXPORT void _array_reserve(void* array, isize item_size, isize to_capacity, Source_Info from);
EXPORT void _array_append(void* array, isize item_size, const void* data, isize data_count, Source_Info from);

#define array_set_capacity(array_ptr, capacity) \
    _array_set_capacity(array_ptr, sizeof *(array_ptr)->_array_data, capacity, SOURCE_INFO())
    
//Initializes the array. If the array is already initialized deinitializes it first.
//Thus expects a properly formed array. Suppling a non-zeroed memory will cause errors!
//All data structers in this library need to be zero init to be valid!
#define array_init(array_ptr, allocator) \
    _array_init(array_ptr, sizeof *(array_ptr)->_array_data, allocator, SOURCE_INFO())
    
//Initializes the array and preallocates it to the desired size
#define array_init_with_capacity(array_ptr, allocator, capacity) \
    array_init((array_ptr), (allocator)), \
    array_reserve((array_ptr), (capacity))

//Deallocates and resets the array
#define array_deinit(array_ptr) \
    _array_deinit(array_ptr, sizeof *(array_ptr)->_array_data, SOURCE_INFO())

//If the array capacity is lower than to_capacity sets the capacity to to_capacity. 
//If setting of capacity is required and the new capcity is less then one geometric growth 
// step away from current capacity grows instead.
#define array_reserve(array_ptr, to_capacity) \
    _array_reserve(array_ptr, sizeof *(array_ptr)->_array_data, to_capacity, SOURCE_INFO()) 

//Sets the array size to the specied to_size. 
//If the to_size is smaller than current size simply dicards further items
//If the to_size is greater than current size zero initializes the newly added items
#define array_resize(array_ptr, to_size)              \
    _array_resize(array_ptr, sizeof *(array_ptr)->_array_data, to_size, true, SOURCE_INFO()) 
   
//Just like array_resize except doesnt zero initialized newly added region
#define array_resize_for_overwrite(array_ptr, to_size)              \
    _array_resize(array_ptr, sizeof *(array_ptr)->_array_data, to_size, false, SOURCE_INFO()) 

//Sets the array size to 0. Does not deallocate the array
#define array_clear(array_ptr) \
    array_resize_for_overwrite(array_ptr, 0)

//Appends item_count items to the end of the array growing it
#define array_append(array_ptr, items, item_count) \
    /* Here is a little hack to typecheck the items array.*/ \
    /* We try to assign the first item to the first data but never actaully run it */ \
    (void) (0 ? *(array_ptr)->_array_data = *(items), 0 : 0), \
    _array_append(array_ptr, sizeof *(array_ptr)->_array_data, items, item_count, SOURCE_INFO())
    
//Discards current items in the array and replaces them with the provided items
#define array_assign(array_ptr, items, item_count) \
    array_clear(array_ptr), \
    array_append(array_ptr, items, item_count)
    
//Copies from copy_from_array into copy_into_array_ptr overriding its elements. 
#define array_copy(copy_into_array_ptr, copy_from_array) \
    array_assign(copy_into_array_ptr, (copy_from_array)._array_data, (copy_from_array).size)

//Appends a single item to the end of the array
#define array_push(array_ptr, item_value)            \
    _array_reserve(array_ptr, sizeof *(array_ptr)->_array_data, (array_ptr)->size + 1, SOURCE_INFO()), \
    (array_ptr)->_array_data[(array_ptr)->size++] = item_value \

//Removes a single item from the end of the array
#define array_pop(array_ptr) \
    ASSERT((array_ptr)->size > 0 && "cannot pop from empty array!"), \
    (array_ptr)->_array_data[--(array_ptr)->size] \
    
//Returns the value of the last item. The array must not be empty!
#define array_last(array) \
    (CHECK_BOUNDS(0, (array).size), &(array)._array_data[(array).size - 1])

//Returns the total size of the array in bytes
#define array_byte_size(array) \
    ((array).size * (isize) sizeof *(array)._array_data)

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARRAY_IMPL)) && !defined(JOT_ARRAY_HAS_IMPL)
#define JOT_ARRAY_HAS_IMPL
#include <string.h>

EXPORT bool _array_is_invariant(const void* array, isize item_size)
{
    u8_Array* base = (u8_Array*) array;
    bool is_capacity_correct = 0 <= base->capacity;
    bool is_size_correct = (0 <= base->size && base->size <= base->capacity);
    if(base->capacity > 0)
        is_capacity_correct = is_capacity_correct && base->allocator != NULL;

    bool is_data_correct = (base->data == NULL) == (base->capacity == 0);
    bool item_size_correct = item_size > 0;
    bool result = is_capacity_correct && is_size_correct && is_data_correct && item_size_correct;
    ASSERT(result);
    return result;
}

EXPORT void _array_init(void* array, isize item_size, Allocator* allocator, Source_Info from)
{
    (void) item_size;
    _array_deinit(array, item_size, from);

    u8_Array* base = (u8_Array*) array;
    base->allocator = allocator;
    if(base->allocator == NULL)
        base->allocator = allocator_get_default();
}

EXPORT void _array_deinit(void* array, isize item_size, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    ASSERT(base != NULL);
    ASSERT(_array_is_invariant(array, item_size));
    if(base->capacity > 0)
        allocator_deallocate(base->allocator, base->data, base->capacity * item_size, DEF_ALIGN, from);
    
    memset(base, 0, sizeof *base);
}

EXPORT void _array_set_capacity(void* array, isize item_size, isize capacity, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(capacity >= 0);

    isize old_byte_size = item_size * base->capacity;
    isize new_byte_size = item_size * capacity;
    if(base->allocator == NULL)
        base->allocator = allocator_get_default();

    base->data = (uint8_t*) allocator_reallocate(base->allocator, new_byte_size, base->data, old_byte_size, DEF_ALIGN, from);

    //trim the size if too big
    base->capacity = capacity;
    if(base->size > base->capacity)
        base->size = base->capacity;
        
    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_resize(void* array, isize item_size, isize to_size, bool zero_new, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    _array_reserve(base, item_size, to_size, from);
    if(zero_new && to_size > base->size)
        memset(base->data + base->size*item_size, 0, (size_t) ((to_size - base->size)*item_size));
        
    base->size = to_size;
    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_reserve(void* array, isize item_size, isize to_fit, Source_Info from)
{
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(to_fit >= 0);
    u8_Array* base = (u8_Array*) array;
    if(base->capacity > to_fit)
        return;
        
    isize new_capacity = to_fit;
    isize growth_step = base->capacity * 3/2 + 8;
    if(new_capacity < growth_step)
        new_capacity = growth_step;

    _array_set_capacity(array, item_size, new_capacity + 1, from);
}

EXPORT void _array_append(void* array, isize item_size, const void* data, isize data_count, Source_Info from)
{
    ASSERT(data_count >= 0 && item_size > 0);
    u8_Array* base = (u8_Array*) array;
    _array_reserve(base, item_size, base->size+data_count, from);
    memcpy(base->data + item_size * base->size, data, (size_t) (item_size * data_count));
    base->size += data_count;
    ASSERT(_array_is_invariant(array, item_size));
}
#endif