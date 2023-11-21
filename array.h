#ifndef LIB_ARRAY
#define LIB_ARRAY

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
// 2) EXPERIMENTAL: The lowest order bit of the allocator pointer to hold if the data needs to be deallocated. 
//    This of course enables quick "borrowing" which can be used to pass data into an interface that accepts
//    a dynamic array without actually needing the full functionality. It can also (and probably more importantly) be used for stack backing
//    arrays - we allocate lets say 128B array on the stack and only dynamically allocate if we need to resize to more than 128B. This can 
//    potentially give us a lot of speed.
//    The downside is that in c we cannot really ensure that the allocator with set bit will be handled properly. 
//    This is to say if we backed allocate an array and then use that allocator to initiaze other structures it will not be correct
//
// 3) Because the functions are implemented as macros we make them all Source_Info transparent. We track locations of all allocations
//    for debugging purposes. Normally we use Source_Info fromn the allocation site - here however we use Source_Info from the use site of the
//    array function.

#include "defines.h"
#include "allocator.h"

#define DEFINE_ARRAY_TYPE(Type, Struct_Name) \
    typedef struct Struct_Name {             \
        Allocator* allocator; /* Lowest order bit indicates wheter is `backed` or not. Do not use directly! */ \
        Type* data;                          \
        int64_t size;                        \
        int64_t capacity;                    \
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
EXPORT void _array_init_backed(void* array, isize item_size, Allocator* allocator, void* backing, int64_t backing_size, Source_Info from);
EXPORT void _array_deinit(void* array, isize item_size, Source_Info from);
EXPORT void _array_set_capacity(void* array, isize item_size, isize capacity, Source_Info from); 
EXPORT void _array_grow_capacity(void* array, isize item_size, isize capacity_at_least, Source_Info from); 
EXPORT bool _array_is_invariant(const void* array, isize item_size);
EXPORT bool _array_is_backed(const void* array, isize item_size);
EXPORT Allocator* _array_get_allocator(const void* array, isize item_size);
EXPORT isize _array_resize(void* array, isize item_size, isize to_size, Source_Info from);
EXPORT void _array_reserve(void* array, isize item_size, isize to_capacity, bool do_growth, Source_Info from);
EXPORT void _array_prepare_push(void* array, isize item_size, Source_Info from);
EXPORT void _array_append(void* array, isize item_size, const void* data, isize data_count, Source_Info from);
EXPORT void _array_unappend(void* array, isize item_size, isize data_count);
EXPORT void _array_clear(void* array, isize item_size);

#define array_set_capacity(array_ptr, capacity) \
    _array_set_capacity(array_ptr, sizeof *(array_ptr)->data, capacity, SOURCE_INFO())
    
//Initializes the array. If the array is already initialized deinitializes it first.
//Thus expects a properly formed array. Suppling a non-zeroed memory will cause errors!
//All data structers in this library need to be zero init to be valid!
#define array_init(array_ptr, allocator) \
    _array_init(array_ptr, sizeof *(array_ptr)->data, allocator, SOURCE_INFO())

//Initializes the array using backed_elements_count elements alloced on the stack as backing store.
//Only triggers a proper reallocation once the capacity required is grater than backed_elements_count.
#ifndef LIB_MEM_DEBUG
    #define array_init_backed(array_ptr, allocator, backed_elements_count) \
        char PP_CONCAT(_backing_buffer_, __LINE__)[(backed_elements_count) * sizeof *(array_ptr)->data]; \
        _array_init_backed(array_ptr, sizeof *(array_ptr)->data, allocator, PP_CONCAT(_backing_buffer_, __LINE__), (backed_elements_count), SOURCE_INFO())
#else
    #define array_init_backed(array_ptr, allocator, backed_elements_count) \
        array_init(array_ptr, allocator)

    //Backing hides small allocations making it harder to catch memory leaks during testing.
    //As sich we disable it when LIB_MEM_DEBUG is on.
#endif // ARRAY_DEBUG

//Initializes the array using backing_array_size elements alloced at backing_array as backing store.
//Only triggers a proper reallocation once the capacity required is grater than backed_elements_count.
#define array_init_backed_from_memory(array_ptr, allocator, backed_elements, backed_elements_count) \
    _array_init_backed(array_ptr, sizeof *(array_ptr)->data, allocator, backed_elements, backed_elements_count, SOURCE_INFO())

//Deallocates and resets the array
#define array_deinit(array_ptr) \
    _array_deinit(array_ptr, sizeof *(array_ptr)->data, SOURCE_INFO())

//Returns the allocator from the array. Raw .allocator field contains lowest bit that indicates whether is backed or not!
#define array_get_allocator(array) \
    _array_get_allocator(&(array), sizeof *(array).data)
    
#define array_is_backed(array) \
    _array_is_backed(&(array), sizeof *(array).data)

//If the array capacity is lower than to_capacity sets the capacity to to_capacity. 
#define array_reserve(array_ptr, to_capacity) \
    _array_reserve(array_ptr, sizeof *(array_ptr)->data, to_capacity, false, SOURCE_INFO()) 

//Grows the array capacity so that capacity is greater or equal to to_capacity. 
//Differes from reserves in that it always follows geometric progression.
#define array_grow(array_ptr, to_capacity) \
    _array_reserve(array_ptr, sizeof *(array_ptr)->data, to_capacity, true, SOURCE_INFO()) 

//Sets the array size to the specied to_size. 
//If the to_size is smaller than current size simply dicards further items
//If the to_size is greater than current size zero initializes the newly added items
#define array_resize(array_ptr, to_size)              \
    _array_resize(array_ptr, sizeof *(array_ptr)->data, to_size, SOURCE_INFO()) 
    
//Sets the array size to 0. Does not deallocate the array
#define array_clear(array_ptr) \
    _array_clear(array_ptr, sizeof *(array_ptr)->data)

//Appends item_count items to the end of the array growing it
#define array_append(array_ptr, items, item_count) \
    /* Here is a little hack to typecheck the items array.*/ \
    /* We try to assign the first item to the first data but never actaully run it */ \
    (void) (0 ? *(array_ptr)->data = *(items), 0 : 0), \
    _array_append(array_ptr, sizeof *(array_ptr)->data, items, item_count, SOURCE_INFO())
    
//Removes item_count items from the end of the array shrinking it
#define array_unappend(array_ptr, item_count) \
    _array_unappend(array_ptr, sizeof *(array_ptr)->data, item_count)
        
//Discards current items in the array and replaces them with the provided items
#define array_assign(array_ptr, items, item_count) \
    array_clear(array_ptr), \
    array_append(array_ptr, items, item_count)
    
//Copies from copy_from_array into copy_into_array_ptr overriding its elements. 
#define array_copy(copy_into_array_ptr, copy_from_array) \
    array_assign(copy_into_array_ptr, (copy_from_array).data, (copy_from_array).size)

//Appends a single item to the end of the array
#define array_push(array_ptr, item_value)            \
    _array_reserve(array_ptr, sizeof *(array_ptr)->data, (array_ptr)->size + 1, true, SOURCE_INFO()), \
    (array_ptr)->data[(array_ptr)->size++] = item_value \

//Removes a single item from the end of the array
#define array_pop(array_ptr) \
    _array_unappend(array_ptr, sizeof *(array_ptr)->data, 1) \
    
//Returns the value of the first item. The array must not be empty!
#define array_first(array) \
    (CHECK_BOUNDS(0, (array).size), &(array).data[0])

//Returns the value of the last item. The array must not be empty!
#define array_last(array) \
    (CHECK_BOUNDS(0, (array).size), &(array).data[(array).size - 1])

//Returns a pointer to i-th item. Also does bounds checking.
#define array_get(array, index) \
    (CHECK_BOUNDS(index, (array).size), &(array).data[index]) 

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_ARRAY_IMPL)) && !defined(LIB_ARRAY_HAS_IMPL)
#define LIB_ARRAY_HAS_IMPL
#include <string.h>

EXPORT bool _array_is_invariant(const void* array, isize item_size)
{
    u8_Array* base = (u8_Array*) array;
    bool is_capaicty_correct = 0 <= base->capacity;
    bool is_size_correct = (0 <= base->size && base->size < base->capacity);
    if(base->size == 0)
        is_size_correct = true;

    bool is_data_correct = (base->data == NULL) == (base->capacity == 0);
    bool item_size_correct = item_size > 0;
    bool result = is_capaicty_correct && is_size_correct && is_data_correct && item_size_correct;
    ASSERT(result);
    return result;
}
EXPORT bool _array_is_backed(const void* array, isize item_size)
{
    (void) item_size;
    u8_Array* base = (u8_Array*) array;
    bool is_backed = (usize) base->allocator & 1;
    return is_backed;
}

INTERNAL Allocator* _set_allocator_bits(Allocator* alloc, bool to)
{
    if(to)
        return (Allocator*) ((usize) alloc | 1);
    else
        return (Allocator*) ((usize) alloc & ~(usize)3);
}

EXPORT Allocator* _array_get_allocator(const void* array, isize item_size)
{
    (void) item_size;
    u8_Array* base = (u8_Array*) array;
    return _set_allocator_bits(base->allocator, false);
}

EXPORT void _array_init(void* array, isize item_size, Allocator* allocator, Source_Info from)
{
    (void) item_size;
    u8_Array* base = (u8_Array*) array;
    _array_deinit(array, item_size, from);
    base->allocator = allocator;
}

EXPORT void _array_init_backed(void* array, isize item_size, Allocator* allocator, void* backing, int64_t backing_item_count, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    _array_deinit(array, item_size, from);

    if(backing_item_count > 0)
    {
        base->data = (u8*) backing;
        base->capacity = backing_item_count;
        base->allocator = _set_allocator_bits(allocator, true);
        memset(base->data, 0, backing_item_count * item_size);
    }
    else
    {
        base->allocator = _set_allocator_bits(allocator, false);
    }

    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_deinit(void* array, isize item_size, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    ASSERT(array != NULL);
    ASSERT(_array_is_invariant(array, item_size));
    if(_array_is_backed(array, item_size) == false && base->data != NULL)
        allocator_deallocate(base->allocator, base->data, base->capacity * item_size, DEF_ALIGN, from);
    
    u8_Array null = {0};
    *base = null;
}

EXPORT void _array_set_capacity(void* array, isize item_size, isize capacity, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(capacity >= 0);

    if(capacity == 0)
    {
        if(_array_is_backed(array, item_size) == false && base->data != NULL)
            allocator_deallocate(base->allocator, base->data, base->capacity * item_size, DEF_ALIGN, from);
            
        base->allocator = _set_allocator_bits(base->allocator, false);
        base->capacity = 0;
        base->size = 0;
        base->data = NULL;
        return;
    }

    isize old_byte_size = item_size * base->capacity;
    isize new_byte_size = item_size * capacity;
    Allocator* alloc = _set_allocator_bits(base->allocator, false);
    if(alloc == NULL)
    {
        base->allocator = allocator_get_default();
        alloc = base->allocator;
    }
        
    if(_array_is_backed(array, item_size))
    {
        isize copy_size = old_byte_size;
        if(copy_size > new_byte_size)
            copy_size = new_byte_size;

        void* new_data = allocator_allocate(alloc, new_byte_size, DEF_ALIGN, from);
        memmove(new_data, base->data, copy_size);
        base->data = (uint8_t*) new_data;
    }
    else
    {
        base->data = (uint8_t*) allocator_reallocate(alloc, new_byte_size, base->data, old_byte_size, DEF_ALIGN, from);
    }
    
    base->allocator = alloc;

    //Clear the allocated to 0
    if(new_byte_size > old_byte_size)
        memset(base->data + old_byte_size, 0, new_byte_size - old_byte_size);

    //trim the size if too big
    base->capacity = capacity;
    if(base->size >= base->capacity && base->capacity != 0)
        base->size = base->capacity - 1;
        
    //escape the string/data
    memset(base->data + base->size*item_size, 0, item_size);
    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_grow_capacity(void* array, isize item_size, isize capacity_at_least, Source_Info from)
{
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(capacity_at_least >= 0);
    
    u8_Array* base = (u8_Array*) array;
    isize new_capacity = base->capacity;
    while(new_capacity < capacity_at_least)
        new_capacity = new_capacity * 3/2 + 8;

    _array_set_capacity(array, item_size, new_capacity, from);
}

EXPORT isize _array_resize(void* array, isize item_size, isize to_size, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    _array_reserve(base, item_size, to_size, false, from);
    isize size_before = base->size;
    if(to_size > base->size)
        memset(base->data + base->size*item_size, 0, (to_size - base->size)*item_size);
        
    base->size = to_size;
    memset(base->data + base->size*item_size, 0, item_size);
    ASSERT(_array_is_invariant(array, item_size));
    return size_before;
}

EXPORT void _array_reserve(void* array, isize item_size, isize to_fit, bool do_growth, Source_Info from)
{
    ASSERT(_array_is_invariant(array, item_size));
    ASSERT(to_fit >= 0);
    u8_Array* base = (u8_Array*) array;
    if(base->capacity > to_fit)
        return;

    if(do_growth)
        _array_grow_capacity(array, item_size, to_fit + 1, from);
    else
        _array_set_capacity(array, item_size, to_fit + 1, from);
}

EXPORT void _array_append(void* array, isize item_size, const void* data, isize data_count, Source_Info from)
{
    u8_Array* base = (u8_Array*) array;
    _array_reserve(base, item_size, base->size+data_count, true, from);
    memmove(base->data + item_size * base->size, data, item_size * data_count);
    base->size += data_count;
    memset(base->data + base->size*item_size, 0, item_size);
    ASSERT(_array_is_invariant(array, item_size));
}

EXPORT void _array_unappend(void* array, isize item_size, isize data_count)
{
    ASSERT(_array_is_invariant(array, item_size));
    u8_Array* base = (u8_Array*) array;
    ASSERT(base->size >= data_count && data_count >= 0);
    base->size -= data_count;

    if(data_count > 0)
        memset(base->data + base->size*item_size, 0, item_size);
}

EXPORT void _array_clear(void* array, isize item_size)
{
    (void) item_size;
    ASSERT(_array_is_invariant(array, item_size));
    u8_Array* base = (u8_Array*) array;
    base->size = 0;
}
#endif