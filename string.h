#ifndef MODULE_STRING
#define MODULE_STRING

#include "allocator.h"
#include "array.h"

typedef Platform_String String;

//A dynamically resizeable string. Its data member is always null 
// terminated even without any allocations. 
// (as long as it was properly initialized - ie. not in = {0} state)
typedef struct String_Builder {
    Allocator* allocator;
    isize capacity;
    //A slightly weird construction so that we can easily obtain a string from the string builder.
    //This saves us from constantly typing `string_from_builder(builder)` and instead just `builder.string`
    union {
        struct {
            char* data;
            isize count;
        };
        String string;
    };
} String_Builder;

typedef Array(String)         String_Array;
typedef Array(String_Builder) String_Builder_Array;

//Constructs a String out of a string literal
#define STRING(cstring) BINIT(String){cstring "", sizeof(cstring "") - 1}


EXTERNAL String string_of(const char* str); //Constructs a string from null terminated str
EXTERNAL String string_make(const char* data, isize size); //Constructs a string
EXTERNAL char   string_at_or(String str, isize at, char if_out_of_range); //returns str.data[at] or if_out_of_range if the index at is not within [0, str.count)
EXTERNAL String string_head(String string, isize to); //keeps only characters to to ( [0, to) interval )
EXTERNAL String string_tail(String string, isize from); //keeps only characters from from ( [from, string.count) interval )
EXTERNAL String string_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXTERNAL String string_safe_head(String string, isize to); //returns string_head using to. If to is outside the range [0, string.count] clamps it to the range. 
EXTERNAL String string_safe_tail(String string, isize from); //returns string_tail using from. If from is outside the range [0, string.count] clamps it to the range. 
EXTERNAL String string_safe_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXTERNAL bool   string_is_equal(String a, String b); //Returns true if the contents and sizes of the strings match
EXTERNAL bool   string_is_prefixed_with(String string, String prefix); 
EXTERNAL bool   string_is_postfixed_with(String string, String postfix);
EXTERNAL bool   string_has_substring_at(String larger_string, isize from_index, String smaller_string); //Returns true if larger_string has smaller_string at index from_index
EXTERNAL int    string_compare(String a, String b); //Compares sizes and then lexographically the contents. Shorter strings are placed before longer ones.

EXTERNAL isize  string_find_first(String string, String search_for, isize from); 
EXTERNAL isize  string_find_first_or(String in_str, String search_for, isize from, isize if_not_found);
EXTERNAL isize  string_find_last_from(String in_str, String search_for, isize from);
EXTERNAL isize  string_find_last(String string, String search_for); 

EXTERNAL isize  string_find_first_char(String string, char search_for, isize from); 
EXTERNAL isize  string_find_first_char_or(String string, char search_for, isize from, isize if_not_found);
EXTERNAL isize  string_find_last_char_from(String in_str, char search_for, isize from);
EXTERNAL isize  string_find_last_char(String string, char search_for); 

EXTERNAL void   string_to_null_terminated(char* buffer, isize buffer_size, String string);
EXTERNAL String string_allocate(Allocator* alloc, String string);
EXTERNAL void   string_deallocate(Allocator* alloc, String* string);

EXTERNAL String_Builder builder_make(Allocator* alloc_or_null, isize capacity_or_zero);
EXTERNAL String_Builder builder_from_cstring(Allocator* allocator, const char* cstring); //Allocates a String_Builder from cstring.
EXTERNAL String_Builder builder_from_string(Allocator* allocator, String string);  //Allocates a String_Builder from String using an allocator.

EXTERNAL void builder_init(String_Builder* builder, Allocator* alloc);
EXTERNAL void builder_init_with_capacity(String_Builder* builder, Allocator* alloc, isize capacity_or_zero);
EXTERNAL void builder_deinit(String_Builder* builder);             
EXTERNAL void builder_set_capacity(String_Builder* builder, isize capacity);             
EXTERNAL void builder_resize(String_Builder* builder, isize capacity);        
EXTERNAL void builder_resize_for_overwrite(String_Builder* builder, isize to_size);
EXTERNAL void builder_reserve(String_Builder* builder, isize capacity);
EXTERNAL void builder_clear(String_Builder* builder);             
EXTERNAL void builder_push(String_Builder* builder, char c);             
EXTERNAL char builder_pop(String_Builder* builder);             
EXTERNAL void builder_append(String_Builder* builder, String string); //Appends a string
EXTERNAL void builder_append_line(String_Builder* builder, String string); //Appends a string followed by newline
EXTERNAL void builder_insert_hole(String_Builder* builder, isize at, isize hole_size, int fill_with_char_or_minus_one);
EXTERNAL void builder_insert(String_Builder* builder, isize at, String string);
EXTERNAL void builder_assign(String_Builder* builder, String string); //Sets the contents of the builder to be equal to string
EXTERNAL bool builder_is_equal(String_Builder a, String_Builder b); //Returns true if the contents and sizes of the strings match
EXTERNAL int  builder_compare(String_Builder a, String_Builder b); //Compares sizes and then lexographically the contents. Shorter strings are placed before longer ones.
EXTERNAL bool builder_is_invariant(String_Builder builder);

EXTERNAL void builder_array_deinit(String_Builder_Array* array);
EXTERNAL String_Builder string_concat(Allocator* allocator, String a, String b);
EXTERNAL String_Builder string_concat3(Allocator* allocator, String a, String b, String c);

//Tiles pattern_size bytes long pattern across field of field_size bytes. 
//The first occurance of pattern is placed at the very start of field and subsequent repetitions follow. 
//If the field_size % pattern_size != 0 the last repetition of pattern is trimmed.
//If pattern_size == 0 field is filled with zeros instead.
EXTERNAL void memtile(void *field, isize field_size, const void* pattern, isize pattern_size);
//Returns the address of the first byte in array pointed to by ptr not equal to value. 
//If all bytes are equal to byte returns NULL. Essentially acts as the opposite of memchr.
EXTERNAL const void* memcheck(const void* ptr, uint8_t value, isize size);
//Swaps the contents of the memory blocks a and b
EXTERNAL void memswap(void* a, void* b, isize size);


EXTERNAL bool char_is_space(char c);
EXTERNAL bool char_is_digit(char c);
EXTERNAL bool char_is_lowercase(char c);
EXTERNAL bool char_is_uppercase(char c);
EXTERNAL bool char_is_alphabetic(char c);
EXTERNAL bool char_is_id(char c);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_STRING)) && !defined(MODULE_HAS_IMPL_STRING)
#define MODULE_HAS_IMPL_STRING
    #include <string.h>
    

    EXTERNAL String string_of(const char* str)
    {
        return string_make(str, str == NULL ? 0 : (isize) strlen(str));
    }

    EXTERNAL String string_make(const char* data, isize size)
    {
        String string = {data, size};
        return string;
    }
    
    EXTERNAL char string_at_or(String str, isize at, char or)
    {
        if((uint64_t) at < (uint64_t) str.count)
            return str.data[at];
        else
            return or;
    }

    EXTERNAL String string_head(String string, isize to)
    {
        CHECK_BOUNDS(to, string.count + 1);
        String head = {string.data, to};
        return head;
    }

    EXTERNAL String string_tail(String string, isize from)
    {
        CHECK_BOUNDS(from, string.count + 1);
        String tail = {string.data + from, string.count - from};
        return tail;
    }
    
    EXTERNAL String string_range(String string, isize from, isize to)
    {
        return string_tail(string_head(string, to), from);
    }

    EXTERNAL String string_safe_head(String string, isize to)
    {
        return string_head(string, CLAMP(to, 0, string.count));
    }
    
    EXTERNAL String string_safe_tail(String string, isize from)
    {
        return string_tail(string, CLAMP(from, 0, string.count));
    }

    EXTERNAL String string_safe_range(String string, isize from, isize to)
    {
        isize escaped_from = CLAMP(from, 0, string.count);
        isize escaped_to = CLAMP(to, 0, string.count);
        return string_range(string, escaped_from, escaped_to);
    }

    EXTERNAL isize string_find_first_or(String in_str, String search_for, isize from, isize if_not_found)
    {
        REQUIRE(from >= 0);

        if(from + search_for.count > in_str.count)
            return if_not_found;
        
        if(search_for.count == 0)
            return from;

        if(search_for.count == 1)
        {
            const char* found = (const char*) memchr(in_str.data + from, search_for.data[0], (size_t) (in_str.count - from));
            if(found == NULL)
                return if_not_found;
            return found - in_str.data;
        }

        const char* found = in_str.data + from;
        char last_char = search_for.data[search_for.count - 1];
        char first_char = search_for.data[0];

        while (true)
        {
            isize remaining_length = in_str.count - (found - in_str.data) - search_for.count + 1;
            ASSERT(remaining_length >= 0);

            found = (const char*) memchr(found, first_char, (size_t) remaining_length);
            if(found == NULL)
                return if_not_found;
                
            char last_char_of_found = found[search_for.count - 1];
            if (last_char_of_found == last_char)
                if (memcmp(found + 1, search_for.data + 1, (size_t) search_for.count - 2) == 0)
                    return found - in_str.data;

            found += 1;
        }

        return if_not_found;
    }
      
    
    EXTERNAL isize string_find_first(String in_str, String search_for, isize from)
    {
        return string_find_first_or(in_str, search_for, from, -1);
    }

    EXTERNAL isize string_find_last_from(String in_str, String search_for, isize from)
    {
        ASSERT(false, "UNTESTED! @TODO: test!");
        ASSERT(from >= 0);
        if(from + search_for.count > in_str.count)
            return -1;

        if(search_for.count == 0)
            return from;

        ASSERT(from >= 0);
        isize start = from;
        if(in_str.count - start < search_for.count)
            start = in_str.count - search_for.count;

        for(isize i = start; i-- > 0; )
        {
            bool found = true;
            for(isize j = 0; j < search_for.count; j++)
            {
                CHECK_BOUNDS(i + j, in_str.count);
                if(in_str.data[i + j] != search_for.data[j])
                {
                    found = false;
                    break;
                }
            }

            if(found)
                return i;
        };

        return -1;
    }

    EXTERNAL isize string_find_last(String in_str, String search_for)
    {
        isize from = MAX(in_str.count - 1, 0);
        return string_find_last_from(in_str, search_for, from);
    }
    
    EXTERNAL isize string_find_first_char_or(String string, char search_for, isize from, isize if_not_found)
    {
        char* ptr = (char*) memchr(string.data + from, search_for, (size_t) (string.count - from));
        return ptr ? (isize) (ptr - string.data) : if_not_found; 
    }
    
    EXTERNAL isize string_find_first_char(String string, char search_for, isize from)
    {
        return string_find_first_char_or(string, search_for, from, -1);
    }

    EXTERNAL void memtile(void *field, isize field_size, const void* pattern, isize pattern_size)
    {
        PROFILE_START();
	    REQUIRE(field_size >= 0 && (field || field_size == 0));
	    REQUIRE(pattern_size >= 0 && (pattern || pattern_size == 0));

        if (field_size <= pattern_size)
            memcpy(field, pattern, (size_t) field_size);
        else if(pattern_size == 0)
            memset(field, 0, (size_t) field_size);
        else
        {
            isize cursor = pattern_size;
            isize copy_size = pattern_size;

            // make one full copy
            memcpy((char*) field, pattern, (size_t) pattern_size);
        
            // now copy from destination buffer, doubling size each iteration
            for (; cursor + copy_size < field_size; copy_size *= 2) 
            {
                memcpy((char*) field + cursor, field, (size_t) copy_size);
                cursor += copy_size;
            }
        
            // copy any remainder
            memcpy((char*) field + cursor, field, (size_t) (field_size - cursor));
        }
        PROFILE_STOP();
    }
    
    EXTERNAL const void* memcheck(const void* ptr, uint8_t byte, isize size)
    {
	    REQUIRE(size >= 0 && (ptr != NULL || size == 0));

        //pattern is 8 repeats of byte
        uint64_t pattern = (uint64_t) 0x0101010101010101ULL * (uint64_t) byte;
        uint8_t* curr = (uint8_t*) ptr;
        uint8_t* end = curr + size;

        while(end - curr >= 32)
        {
            uint64_t copied[4] = {0};
            memcpy(copied, curr, 32);
            if(copied[0] != pattern
                || copied[1] != pattern
                || copied[2] != pattern
                || copied[3] != pattern) 
                break;
            curr += 32;
        }

        while(end - curr >= 8)
        {
            uint64_t copied = {0};
            memcpy(&copied, curr, 8);
            if(copied != pattern)
                break; 
            curr += 8;
        }
        
        for(; end != curr; curr ++)
        {
            if(*curr != byte)
                return curr;
        }

        return NULL;
    }
    
    EXTERNAL void memswap_generic(void* a, void* b, isize size)
    {
        PROFILE_START();
	    REQUIRE(size >= 0 && ((a && b) || size == 0));
        enum {LOCAL = 8};
        char temp[LOCAL] = {0};

        char* ac = (char*) a;
        char* bc = (char*) b;
    
        size_t repeats = (size_t) size / LOCAL;
        size_t remainder = (size_t) size % LOCAL;
        for(size_t k = 0; k < repeats; k ++)
        {
	        memcpy(temp,         ac + k*LOCAL, LOCAL);
	        memcpy(ac + k*LOCAL, bc + k*LOCAL, LOCAL);
	        memcpy(bc + k*LOCAL, temp,         LOCAL);
        }

        ac += repeats*LOCAL;
        bc += repeats*LOCAL;
        for(size_t i = 0; i < remainder; i++)
        {
            char t = ac[i];
            ac[i] = bc[i];
            bc[i] = t;         
        }
        PROFILE_STOP();
    }

    EXTERNAL void memswap(void* a, void* b, isize size)
    {
	    REQUIRE(size >= 0 && ((a && b) || size == 0));
        PROFILE_START();
        char temp[32] = {0};
        switch(size) {
            #define SWAP_X(N) \
                case N: { \
                    memcpy(temp, a, N); \
                    memcpy(a, b, N); \
                    memcpy(b, temp, N); \
                } break;

            SWAP_X(4)
            SWAP_X(8)
            SWAP_X(12)
            SWAP_X(16)
            SWAP_X(20)
            SWAP_X(24)
            SWAP_X(28)
            SWAP_X(32)
            #undef SWAP_X

            default: memswap_generic(a, b, size); break;
        }
        PROFILE_STOP();
    }

    EXTERNAL isize string_find_last_char_from(String string, char search_for, isize from)
    {
        for(isize i = from + 1; i-- > 0; )
            if(string.data[i] == search_for)
                return i;

        return -1;
    }
    
    EXTERNAL isize string_find_last_char(String string, char search_for)
    {
        return string_find_last_char_from(string, search_for, string.count - 1);
    }

    EXTERNAL int string_compare(String a, String b)
    {
        if(a.count > b.count)
            return -1;
        if(a.count < b.count)
            return 1;

        int res = memcmp(a.data, b.data, (u64) a.count);
        return res;
    }
    
    EXTERNAL bool string_is_equal(String a, String b)
    {
        if(a.count != b.count)
            return false;

        bool eq = memcmp(a.data, b.data, (u64) a.count) == 0;
        return eq;
    }

    EXTERNAL bool string_is_prefixed_with(String string, String prefix)
    {
        if(string.count < prefix.count)
            return false;

        String trimmed = string_head(string, prefix.count);
        return string_is_equal(trimmed, prefix);
    }

    EXTERNAL bool string_is_postfixed_with(String string, String postfix)
    {
        if(string.count < postfix.count)
            return false;

        String trimmed = string_tail(string, postfix.count);
        return string_is_equal(trimmed, postfix);
    }

    EXTERNAL bool string_has_substring_at(String larger_string, isize from_index, String smaller_string)
    {
        if(larger_string.count - from_index < smaller_string.count)
            return false;

        String portion = string_range(larger_string, from_index, from_index + smaller_string.count);
        return string_is_equal(portion, smaller_string);
    }
    
    EXTERNAL String_Builder string_concat(Allocator* allocator, String a, String b)
    {
        String_Builder out = builder_make(allocator, a.count + b.count);
        builder_append(&out, a);
        builder_append(&out, b);
        return out;
    }

    EXTERNAL String_Builder string_concat3(Allocator* allocator, String a, String b, String c)
    {
        String_Builder out = builder_make(allocator, a.count + b.count + c.count);
        builder_append(&out, a);
        builder_append(&out, b);
        builder_append(&out, c);
        return out;
    }
    
    EXTERNAL void string_to_null_terminated(char* buffer, isize buffer_size, String string)
    {
        if(buffer_size > 0)
        {
            isize min_size = MIN(buffer_size - 1, string.count);
            memcpy(buffer, string.data, (size_t) min_size);
            buffer[min_size] = '\0';
        }
    }

    EXTERNAL String string_allocate(Allocator* alloc, String string)
    {
        PROFILE_START();
        char* data = allocator_allocate(alloc, string.count + 1, 1);
        memcpy(data, string.data, (size_t) string.count);
        data[string.count] = '\0';
        String out = {data, string.count};
        PROFILE_STOP();
        return out;
    }

    EXTERNAL void string_deallocate(Allocator* alloc, String* string)
    {
        PROFILE_START();
        if(string->count != 0)
            allocator_deallocate(alloc, (void*) string->data, string->count + 1, 1);
        String nil = {0};
        *string = nil;
        PROFILE_STOP();
    }
    
    char _builder_null_termination[4] = {0};
    EXTERNAL bool builder_is_invariant(String_Builder builder)
    {
        bool null_termination_not_corrupted = _builder_null_termination[0] == '\0';
        bool is_capacity_correct = 0 <= builder.capacity;
        bool is_size_correct = (0 <= builder.count && builder.count <= builder.capacity);
        //Data is default iff capacity is zero
        bool is_data_correct = (builder.data == NULL || builder.data == _builder_null_termination) == (builder.capacity == 0);

        //If has capacity then was allocated therefore must have an allocator set
        if(builder.capacity > 0)
            is_capacity_correct = is_capacity_correct && builder.allocator != NULL;

        //If is not in 0 state must be null terminated (both right after and after the whole capacity for safety)
        bool is_null_terminated = true;
        if(builder.data != NULL)
            is_null_terminated = builder.data[builder.count] == '\0' && builder.data[builder.capacity] == '\0';
        
        bool result = is_capacity_correct && is_size_correct && is_data_correct && is_null_terminated && null_termination_not_corrupted;
        ASSERT(result);
        return result;
    }
    EXTERNAL void builder_deinit(String_Builder* builder)
    {
        ASSERT_SLOW(builder_is_invariant(*builder));

        if(builder->data != NULL && builder->data != _builder_null_termination)
            allocator_deallocate(builder->allocator, builder->data, builder->capacity + 1, 1);
    
        memset(builder, 0, sizeof *builder);
    }
    
    EXTERNAL void builder_init(String_Builder* builder, Allocator* allocator)
    {
        builder_deinit(builder);
        builder->allocator = allocator;
        builder->data = _builder_null_termination;
        REQUIRE(allocator != NULL);
    }

    EXTERNAL void builder_init_with_capacity(String_Builder* builder, Allocator* allocator, isize capacity_or_zero)
    {
        builder_init(builder, allocator);
        if(capacity_or_zero > 0)
            builder_set_capacity(builder, capacity_or_zero);
    }

    EXTERNAL String_Builder builder_make(Allocator* alloc_or_null, isize capacity_or_zero)
    {
        String_Builder builder = {0};
        builder.allocator = alloc_or_null;
        builder.data = _builder_null_termination;
        if(capacity_or_zero > 0)
            builder_set_capacity(&builder, capacity_or_zero);
        return builder;
    }

    EXTERNAL void builder_set_capacity(String_Builder* builder, isize capacity)
    {
        PROFILE_START();
        ASSERT_SLOW(builder_is_invariant(*builder));
        REQUIRE(capacity >= 0 && builder && builder->allocator != NULL);

        void* old_data = NULL;
        isize old_alloced = 0;
        isize new_alloced = capacity + 1;

        //Make sure we are taking into account the null terminator properly when deallocating.
        //This is rather tricky because: 
        //  when capacity >  0 we have allocated capacity + 1 bytes
        //  when capacity <= 0 we have allocated nothing
        //Keep in mind our allocator_reallocate works as both alloc, realloc, and free all at once
        // so we make sure to call it only once for all cases (so that arenas can be inlined better)
        if(builder->data != NULL && builder->data != _builder_null_termination)
        {
            ASSERT(builder->capacity > 0);
            old_data = builder->data;
            old_alloced = builder->capacity + 1;
        }
        if(capacity == 0)
            new_alloced = 0;

        builder->data = (char*) allocator_reallocate(builder->allocator, new_alloced, old_data, old_alloced, 1);
        
        //Always memset the new capacity to zero so that that we dont have to set null termination
        //while pushing
        if(new_alloced > old_alloced)
            memset(builder->data + old_alloced, 0, (size_t) (new_alloced - old_alloced));

        //trim the size if too big
        builder->capacity = capacity;
        if(builder->count > builder->capacity)
            builder->count = builder->capacity;
        
        //Restore null termination
        if(capacity == 0)
            builder->data = _builder_null_termination;
        else
        {
            builder->data[builder->count] = '\0'; 
            builder->data[builder->capacity] = '\0'; 
        }
        PROFILE_STOP();
        ASSERT_SLOW(builder_is_invariant(*builder));
    }
    
    EXTERNAL void builder_reserve(String_Builder* builder, isize to_fit)
    {
        ASSERT(to_fit >= 0);
        if(builder->capacity > to_fit)
            return;
        
        isize new_capacity = to_fit;
        isize growth_step = builder->capacity * 3/2 + 8;
        if(new_capacity < growth_step)
            new_capacity = growth_step;

        builder_set_capacity(builder, new_capacity);
    }

    EXTERNAL void builder_resize_for_overwrite(String_Builder* builder, isize to_size)
    {
        builder_reserve(builder, to_size);
        if(to_size >= builder->count)
            builder->data[to_size] = '\0';
        else
            //We clear the memory when shrinking so that we dont have to clear it when pushing!
            memset(builder->data + to_size, 0, (size_t) ((builder->count - to_size)));
    }

    EXTERNAL void builder_resize(String_Builder* builder, isize to_size)
    {
        builder_reserve(builder, to_size);
        if(to_size >= builder->count)
            memset(builder->data + builder->count, 0, (size_t) ((to_size - builder->count)));
        else
            //We clear the memory when shrinking so that we dont have to clear it when pushing!
            memset(builder->data + to_size, 0, (size_t) ((builder->count - to_size)));
        
        builder->count = to_size;
        ASSERT_SLOW(builder_is_invariant(*builder));
    }

    EXTERNAL void builder_clear(String_Builder* builder)
    {
        builder_resize(builder, 0);
    }

    EXTERNAL void builder_append(String_Builder* builder, String string)
    {
        ASSERT(string.count >= 0);
        builder_reserve(builder, builder->count+string.count);
        memcpy(builder->data + builder->count, string.data, (size_t) string.count);
        builder->count += string.count;
        ASSERT_SLOW(builder_is_invariant(*builder));
    }

    EXTERNAL void builder_append_line(String_Builder* builder, String string)
    {
        ASSERT(string.count >= 0);
        builder_reserve(builder, builder->count+string.count + 1);
        memcpy(builder->data + builder->count, string.data, (size_t) string.count);
        builder->data[builder->count + string.count] = '\n';
        builder->count += string.count + 1;
        ASSERT_SLOW(builder_is_invariant(*builder));
    }
    
    EXTERNAL void builder_insert_hole(String_Builder* builder, isize at, isize hole_size, int fill_with_char_or_minus_one)
    {
        ASSERT_BOUNDS(0 <= at && at <= builder->count);
        builder_reserve(builder, builder->count + hole_size);
        memmove(builder->data + at + hole_size, builder->data + at, (size_t) hole_size);
        if(fill_with_char_or_minus_one != -1)
            memset(builder->data + at, fill_with_char_or_minus_one, (size_t) hole_size);

        builder->count += hole_size;
        builder->data[builder->count] = '\0';
    }
    
    EXTERNAL void builder_insert(String_Builder* builder, isize at, String string)
    {
        builder_insert_hole(builder, at, string.count, -1);
        memcpy(builder->data + at, string.data, (size_t) string.count);
    }

    EXTERNAL void builder_assign(String_Builder* builder, String string)
    {
        builder_resize(builder, string.count);
        memcpy(builder->data, string.data, (size_t) string.count);
        ASSERT_SLOW(builder_is_invariant(*builder));
    }

    EXTERNAL void builder_push(String_Builder* builder, char c)
    {
        builder_reserve(builder, builder->count+1);
        builder->data[builder->count++] = c;
    }

    EXTERNAL char builder_pop(String_Builder* builder)
    {
        ASSERT_BOUNDS(builder->count > 0);
        char popped = builder->data[--builder->count];
        builder->data[builder->count] = '\0';
        return popped;
    }
    
    EXTERNAL void builder_array_deinit(String_Builder_Array* array)
    {
        for(isize i = 0; i < array->count; i++)
            builder_deinit(&array->data[i]);

        array_deinit(array);
    }
    
    EXTERNAL String_Builder builder_from_string(Allocator* allocator, String string)
    {
        String_Builder builder = builder_make(allocator, string.count);
        if(string.count)
            builder_assign(&builder, string);
        return builder;
    }

    EXTERNAL String_Builder builder_from_cstring(Allocator* allocator, const char* cstring)
    {
        return builder_from_string(allocator, string_of(cstring));
    }

    EXTERNAL bool builder_is_equal(String_Builder a, String_Builder b)
    {
        return string_is_equal(a.string, b.string);
    }
    
    EXTERNAL int builder_compare(String_Builder a, String_Builder b)
    {
        return string_compare(a.string, b.string);
    }

    EXTERNAL bool char_is_space(char c)
    {
        return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
    }

    EXTERNAL bool char_is_digit(char c)
    {
        return '0' <= c && c <= '9';
    }

    EXTERNAL bool char_is_lowercase(char c)
    {
        return 'a' <= c && c <= 'z';
    }

    EXTERNAL bool char_is_uppercase(char c)
    {
        return 'A' <= c && c <= 'Z';
    }

    EXTERNAL bool char_is_alphabetic(char c)
    {
        //return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');

        //this is just a cute way of doing the two range checks in one
        //using the fact that all uppercase to lowercase letters are 32 apart.
        //That means we can just makes the fifth bit and test once.
        //You can simply test this works by comparing the result of both approaches on all char values.
        char masked = (c - 'A') & ~(1 << 5);
        bool is_letter = 0 <= masked && masked <= ('Z' - 'A');

        return is_letter;
    }

    EXTERNAL char char_to_upper(char c)
    {
        if('a' <= c && c <= 'z')
            return c - 'a' + 'A';
        else    
            return c;
    }
    
    EXTERNAL char char_to_lower(char c)
    {
        if('A' <= c && c <= 'A')
            return c - 'A' + 'a';
        else    
            return c;
    }

    //all characters permitted inside a common programming language id. [0-9], _, [a-z], [A-Z]
    EXTERNAL bool char_is_id(char c)
    {
        return char_is_digit(c) || char_is_alphabetic(c) || c == '_';
    }
#endif