#ifndef JOT_ARRAY
#define JOT_ARRAY

// This freestanding file introduces a simple but powerful generic dynamic array concept.
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
// This file is also fully freestanding. To compile the function definitions #define JOT_ALL_IMPL and include it again in .c file. 

#if !defined(JOT_INLINE_ALLOCATOR) && !defined(JOT_ALLOCATOR)
    #define JOT_INLINE_ALLOCATOR
    #include <stdint.h>
    #include <stdlib.h>
    #include <assert.h>
    
    #define EXPORT
    #define DEF_ALIGN sizeof(void*)
    #define ASSERT(x) assert(x)

    typedef struct Allocator Allocator;
    static Allocator* allocator_get_default() { return NULL; }
    static void* allocator_reallocate(Allocator* from_allocator, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
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

typedef struct Generic_Array {        
    Allocator* allocator;                
    uint8_t* data;                      
    int64_t size;                        
    int64_t capacity;                    
} Generic_Array;

#define Array(Type) \
    union {             \
        Generic_Array generic;                   \
        struct {                                 \
            Allocator* allocator;                \
            Type* data;                          \
            int64_t size;                        \
            int64_t capacity;                    \
        };                                       \
    }                                            \

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

EXPORT void _array_init(Generic_Array* array, int64_t item_size, Allocator* allocator);
EXPORT void _array_deinit(Generic_Array* array, int64_t item_size);
EXPORT void _array_set_capacity(Generic_Array* array, int64_t item_size, int64_t capacity); 
EXPORT int  _array_is_invariant(Generic_Array* array, int64_t item_size);
EXPORT void _array_resize(Generic_Array* array, int64_t item_size, int64_t to_size, int zero_new);
EXPORT void _array_reserve(Generic_Array* array, int64_t item_size, int64_t to_capacity);
EXPORT void _array_append(Generic_Array* array, int64_t item_size, const void* data, int64_t data_count);

#define array_set_capacity(array_ptr, capacity) \
    _array_set_capacity(&(array_ptr)->generic, sizeof *(array_ptr)->data, (capacity))
    
//Initializes the array. If the array is already initialized deinitializes it first.
//Thus expects a properly formed array. Suppling a non-zeroed memory will cause errors!
//All data structers in this library need to be zero init to be valid!
#define array_init(array_ptr, allocator) \
    _array_init(&(array_ptr)->generic, sizeof *(array_ptr)->data, (allocator))
    
//Initializes the array and preallocates it to the desired size
#define array_init_with_capacity(array_ptr, allocator, capacity) (\
        array_init((array_ptr), (allocator)), \
        array_reserve((array_ptr), (capacity)) \
    )

//Deallocates and resets the array
#define array_deinit(array_ptr) \
    _array_deinit(&(array_ptr)->generic, sizeof *(array_ptr)->data)

//If the array capacity is lower than to_capacity sets the capacity to to_capacity. 
//If setting of capacity is required and the new capcity is less then one geometric growth 
// step away from current capacity grows instead.
#define array_reserve(array_ptr, to_capacity) \
    _array_reserve(&(array_ptr)->generic, sizeof *(array_ptr)->data, (to_capacity)) 

//Sets the array size to the specied to_size. 
//If the to_size is smaller than current size simply dicards further items
//If the to_size is greater than current size zero initializes the newly added items
#define array_resize(array_ptr, to_size)              \
    _array_resize(&(array_ptr)->generic, sizeof *(array_ptr)->data, (to_size), 1) 
   
//Just like array_resize except doesnt zero initialized newly added region
#define array_resize_for_overwrite(array_ptr, to_size)              \
    _array_resize(&(array_ptr)->generic, sizeof *(array_ptr)->data, (to_size), 0) 

//Sets the array size to 0. Does not deallocate the array
#define array_clear(array_ptr) \
    array_resize_for_overwrite((array_ptr), 0)

//Appends item_count items to the end of the array growing it
#define array_append(array_ptr, items, item_count) (\
        /* Here is a little hack to typecheck the items array.*/ \
        /* We do a comparison that emmits a warning on incompatible types but doesnt get executed */ \
        sizeof((array_ptr)->data == (items)), \
        _array_append(&(array_ptr)->generic, sizeof *(array_ptr)->data, (items), (item_count)) \
    ) \
    
//Discards current items in the array and replaces them with the provided items
#define array_assign(array_ptr, items, item_count) \
    array_clear(array_ptr), \
    array_append((array_ptr), (items), (item_count))
    
//Copies from from_arr into to_arr_ptr overriding its elements. 
#define array_copy(to_arr_ptr, from_arr) \
    array_assign((to_arr_ptr), (from_arr).data, (from_arr).size)

//Appends a single item to the end of the array
#define array_push(array_ptr, item_value) (           \
        _array_reserve(&(array_ptr)->generic, sizeof *(array_ptr)->data, (array_ptr)->size + 1), \
        (array_ptr)->data[(array_ptr)->size++] = (item_value) \
    ) \

//Removes a single item from the end of the array
#define array_pop(array_ptr) (\
        ASSERT((array_ptr)->size > 0 && "cannot pop from empty array!"), \
        (array_ptr)->data[--(array_ptr)->size] \
    ) \
    
//Returns the value of the last item. The array must not be empty!
#define array_last(array) (\
        ASSERT((array).size > 0 && "cannot get last from empty array!"), \
        &(array).data[(array).size - 1] \
    ) \

//Returns the total size of the array in bytes
#define array_byte_size(array) \
    ((array).size * isizeof *(array).data)

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARRAY_IMPL)) && !defined(JOT_ARRAY_HAS_IMPL)
#define JOT_ARRAY_HAS_IMPL
#include <string.h>

EXPORT int _array_is_invariant(Generic_Array* array, int64_t item_size)
{
    int is_capacity_correct = 0 <= array->capacity;
    int is_size_correct = (0 <= array->size && array->size <= array->capacity);
    if(array->capacity > 0)
        is_capacity_correct = is_capacity_correct && array->allocator != NULL;

    int is_data_correct = (array->data == NULL) == (array->capacity == 0);
    int item_size_correct = item_size > 0;
    int result = is_capacity_correct && is_size_correct && is_data_correct && item_size_correct;
    ASSERT(result);
    return result;
}

EXPORT void _array_init(Generic_Array* array, int64_t item_size, Allocator* allocator)
{
    (void) item_size;
    _array_deinit(array, item_size);

    array->allocator = allocator;
    if(array->allocator == NULL)
        array->allocator = allocator_get_default();
}

EXPORT void _array_deinit(Generic_Array* array, int64_t item_size)
{
    ASSERT(array != NULL);
    ASSERT(_array_is_invariant(array, item_size));
    if(array->capacity > 0)
        allocator_reallocate(array->allocator, 0, array->data, array->capacity * item_size, DEF_ALIGN);
    
    memset(array, 0, sizeof *array);
}

EXPORT void _array_set_capacity(Generic_Array* array, int64_t item_size, int64_t capacity)
{
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(capacity >= 0);

    int64_t old_byte_size = item_size * array->capacity;
    int64_t new_byte_size = item_size * capacity;
    if(array->allocator == NULL)
        array->allocator = allocator_get_default();

    array->data = (uint8_t*) allocator_reallocate(array->allocator, new_byte_size, array->data, old_byte_size, DEF_ALIGN);

    //trim the size if too big
    array->capacity = capacity;
    if(array->size > array->capacity)
        array->size = array->capacity;
        
    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_resize(Generic_Array* array, int64_t item_size, int64_t to_size, int zero_new)
{
    _array_reserve(array, item_size, to_size);
    if(zero_new && to_size > array->size)
        memset(array->data + array->size*item_size, 0, (size_t) ((to_size - array->size)*item_size));
        
    array->size = to_size;
    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_reserve(Generic_Array* array, int64_t item_size, int64_t to_fit)
{
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(to_fit >= 0);
    if(array->capacity > to_fit)
        return;
        
    int64_t new_capacity = to_fit;
    int64_t growth_step = array->capacity * 3/2 + 8;
    if(new_capacity < growth_step)
        new_capacity = growth_step;

    _array_set_capacity(array, item_size, new_capacity + 1);
}

EXPORT void _array_append(Generic_Array* array, int64_t item_size, const void* data, int64_t data_count)
{
    ASSERT(data_count >= 0 && item_size > 0);
    _array_reserve(array, item_size, array->size+data_count);
    memcpy(array->data + item_size * array->size, data, (size_t) (item_size * data_count));
    array->size += data_count;
    ASSERT(_array_is_invariant(array, item_size));
}
#endif