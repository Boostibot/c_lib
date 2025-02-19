#ifndef MODULE_STRING
#define MODULE_STRING

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef MODULE_ALL_COUPLED
    #include "assert.h"
    #include "profile.h"
    #include "allocator.h"
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

//Slice-like string. 
typedef struct String {
    const char* data;
    isize count;
} String;

//A dynamically resizeable string. Its data member is always null terminated. 
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

//Constructs a String out of a string literal
#ifdef __cplusplus
    #define STRING(cstring) String{cstring, sizeof(cstring"") - 1}
#else
    #define STRING(cstring) SINIT(String){cstring, sizeof(cstring"") - 1}
#endif 

#ifndef EXTERNAL
    #define EXTERNAL
#endif

EXTERNAL String string_of(const char* str); //Constructs a string from null terminated str
EXTERNAL String string_make(const char* data, isize size); //Constructs a string
EXTERNAL char   string_at_or(String str, isize at, char if_out_of_range); //returns str.data[at] or if_out_of_range if the index at is not within [0, str.count)
EXTERNAL String string_head(String string, isize to); //returns the interval [0, to) of string
EXTERNAL String string_tail(String string, isize from); // returns the interval [from, string.count) of string 
EXTERNAL String string_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXTERNAL String string_safe_head(String string, isize to); //returns string_head using to. If to is outside the range [0, string.count] clamps it to the range. 
EXTERNAL String string_safe_tail(String string, isize from); //returns string_tail using from. If from is outside the range [0, string.count] clamps it to the range. 
EXTERNAL String string_safe_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXTERNAL bool   string_is_equal(String a, String b); //Returns true if the sizes and contents of the strings match
EXTERNAL bool   string_is_equal_nocase(String a, String b); //Returns true if the sizes and contents of the strings match when ascii of both is converted to lowercase
EXTERNAL bool   string_is_prefixed_with(String string, String prefix); 
EXTERNAL bool   string_is_postfixed_with(String string, String postfix);
EXTERNAL bool   string_has_substring_at(String string, String substring, isize at_index); //Returns true if string has substring at index from_index
EXTERNAL int    string_compare(String a, String b); //Compares sizes and then lexicographically the contents. Shorter strings are placed before longer ones.
EXTERNAL int    string_compare_lexicographic(String a, String b); //Compares lexicographically the contents then the sizes. Shorter strings are placed before longer ones.

EXTERNAL String string_trim_prefix_whitespace(String s);    //" \t\n abc   " ->       "abc   "
EXTERNAL String string_trim_postfix_whitespace(String s);   //" \t\n abc   " -> " \t\n abc"
EXTERNAL String string_trim_whitespace(String s);           //" \t\n abc   " ->       "abc"

EXTERNAL isize  string_find_first(String in_str, String search_for, isize from); //returns the first index of search_for in in_str within [from, string.count) or -1 if no index exists
EXTERNAL isize  string_find_last(String in_str, String search_for, isize from);  //returns the last index of search_for in in_str within [from, string.count) or -1 if no index exists
EXTERNAL isize  string_find_first_char(String in_str, char search_for, isize from); //same but only searches a single char
EXTERNAL isize  string_find_last_char(String in_str, char search_for, isize from); //same but only searches a single char

EXTERNAL isize  string_find_first_or(String in_str, String search_for, isize from, isize if_not_found);
EXTERNAL isize  string_find_last_or(String in_str, String search_for, isize from, isize if_not_found);
EXTERNAL isize  string_find_first_char_or(String in_str, char search_for, isize from, isize if_not_found);
EXTERNAL isize  string_find_last_char_or(String in_str, char search_for, isize from, isize if_not_found);

EXTERNAL isize  string_null_terminate(char* buffer, isize buffer_size, String string);  //writes into buffer at max buffer_size chars from string. returns the amount of chars written not including null termination.
EXTERNAL String string_allocate(Allocator* alloc, String string);
EXTERNAL void   string_deallocate(Allocator* alloc, String* string);
EXTERNAL void   string_reallocate(Allocator* alloc, String* string, String new_val);

#define BUILDER_REISIZE_FOR_OVERWRITE -1
EXTERNAL String_Builder builder_make(Allocator* alloc_or_null, isize capacity_or_zero);
EXTERNAL String_Builder builder_of(Allocator* allocator, String string);  //Allocates a String_Builder from String using an allocator.
EXTERNAL void builder_init(String_Builder* builder, Allocator* alloc);
EXTERNAL void builder_init_with_capacity(String_Builder* builder, Allocator* alloc, isize capacity_or_zero);
EXTERNAL void builder_deinit(String_Builder* builder);             
EXTERNAL void builder_set_capacity(String_Builder* builder, isize capacity);             
EXTERNAL void builder_resize(String_Builder* builder, isize capacity, int fill_with_char_or_minus_one);        
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
EXTERNAL int  builder_compare(String_Builder a, String_Builder b); //Compares sizes and then lexicographically the contents. Shorter strings are placed before longer ones.
EXTERNAL bool builder_is_invariant(String_Builder builder);

EXTERNAL String_Builder string_concat(Allocator* allocator, String a, String b);
EXTERNAL String_Builder string_concat3(Allocator* allocator, String a, String b, String c);

//Format functions. We also have a macro wrapper for these which fake evaluate printf 
// (its evaluated inside sizeof so it doesnt actually get run). This makes the compiler typecheck the arguments.
EXTERNAL void vformat_append_into(String_Builder* append_to, const char* format, va_list args);
EXTERNAL void _format_append_into(String_Builder* append_to, const char* format, ...);
EXTERNAL void vformat_into(String_Builder* into, const char* format, va_list args);
EXTERNAL void _format_into(String_Builder* into, const char* format, ...);
EXTERNAL String_Builder vformat(Allocator* alloc, const char* format, va_list args);
EXTERNAL String_Builder _format(Allocator* alloc, const char* format, ...);

#define  format_into(into, format, ...)             ((void) sizeof printf((format), ##__VA_ARGS__), _format_append_into((into), (format), ##__VA_ARGS__))
#define  format_append_into(append_to, format, ...) ((void) sizeof printf((format), ##__VA_ARGS__), _format_append_into((append_to), (format), ##__VA_ARGS__))
#define  format(allocator, format, ...)             ((void) sizeof printf((format), ##__VA_ARGS__), _format((allocator), (format), ##__VA_ARGS__))

//ptrs - these functions do the exact same thing as their non ptrs counterparts except take pointers, which is sometimes
// useful for things like qsort or my map implementation
EXTERNAL int  string_compare_ptrs(const String* a, const String* b); 
EXTERNAL bool string_is_equal_ptrs(const String* a, const String* b);
EXTERNAL bool builder_is_equal_ptrs(const String_Builder* a, const String_Builder* b);
EXTERNAL int  builder_compare_ptrs(const String_Builder* a, const String_Builder* b);

EXTERNAL bool char_is_space(char c);    
EXTERNAL bool char_is_digit(char c); //[0-9]
EXTERNAL bool char_is_lower(char c); //[a-z]
EXTERNAL bool char_is_upper(char c); //[A-Z]
EXTERNAL bool char_is_alpha(char c); //[A-Z] | [a-z]

EXTERNAL char char_to_upper(char c);
EXTERNAL char char_to_lower(char c);

//Line iteration - handles line terminations for linux, windows, mac - "\n", "\r\n", "\r"
//use like so: for(Line_Iterator it = {0}; line_iterator_next(&it, string); ) {...}
typedef struct Line_Iterator {
    String line;
    isize line_number; //one based line number
    isize line_from; 
    isize line_to;  
    isize next_line_from;
} Line_Iterator;

EXTERNAL bool line_iterator_next(Line_Iterator* iterator, String string);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_STRING)) && !defined(MODULE_HAS_IMPL_STRING)
#define MODULE_HAS_IMPL_STRING
    #ifndef PROFILE_START
        #define PROFILE_START(...)
        #define PROFILE_STOP(...)
        #define PROFILE_INSTANT(...)
    #endif

    #ifndef ASSERT
        #include <assert.h>
        #define ASSERT(x, ...)              assert(x)
        #define ASSERT_SLOW(x, ...)         assert(x)
        #define REQUIRE(x, ...)             assert(x)
        #define CHECK_BOUNDS(i, count, ...) assert(0 <= (i) && (i) <= count)
    #endif  

    EXTERNAL String string_of(const char* str)
    {
        return string_make(str, str == NULL ? 0 : (isize) strlen(str));
    }

    EXTERNAL String string_make(const char* data, isize size)
    {
        String string = {data, size};
        return string;
    }
    
    EXTERNAL char string_at_or(String str, isize at, char if_out_of_range)
    {
        if((uint64_t) at < (uint64_t) str.count)
            return str.data[at];
        else
            return if_out_of_range;
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

    #define _CLAMP(val, min, max) ((val) < (min) ? min : (val) > (max) ? (max) : (val))
    EXTERNAL String string_safe_head(String string, isize to)
    {
        return string_head(string, _CLAMP(to, 0, string.count));
    }
    
    EXTERNAL String string_safe_tail(String string, isize from)
    {
        return string_tail(string, _CLAMP(from, 0, string.count));
    }

    EXTERNAL String string_safe_range(String string, isize from, isize to)
    {
        isize clamped_to = _CLAMP(to, 0, string.count);
        isize clamped_from = _CLAMP(from, 0, to);
        return string_range(string, clamped_from, clamped_to);
    }
    #undef _CLAMP

    EXTERNAL isize string_find_first_or(String in_str, String search_for, isize from, isize if_not_found)
    {
        if(from < 0 || from + search_for.count > in_str.count)
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

    EXTERNAL isize string_find_last_or(String in_str, String search_for, isize from, isize if_not_found)
    {
        if(from < 0 || from + search_for.count > in_str.count)
            return if_not_found;

        if(search_for.count == 0)
            return from;

        isize to = in_str.count - in_str.count; 
        for(isize i = to; i >= from; i--)
            if(memcmp(in_str.data + i, search_for.data, search_for.count) == 0)
                return i;

        return if_not_found;
    }
    
    EXTERNAL isize string_find_first_char_or(String string, char search_for, isize from, isize if_not_found)
    {
        if(from < 0 || from >= string.count)
            return if_not_found;
        char* ptr = (char*) memchr(string.data + from, search_for, (size_t) (string.count - from));
        return ptr ? (isize) (ptr - string.data) : if_not_found; 
    }
    
    EXTERNAL isize string_find_last_char_or(String in_str, char search_for, isize from, isize if_not_found)
    {
        if(from < 0 || from >= in_str.count)
            return if_not_found;
            
        for(isize i = in_str.count; i-- > from; )
            if(in_str.data[i] == search_for)
                return i;

        return if_not_found;
    }
    
    EXTERNAL isize string_find_first(String in_str, String search_for, isize from)
    {
        return string_find_first_or(in_str, search_for, from, -1);
    }
    EXTERNAL isize string_find_last(String in_str, String search_for, isize from)
    {
        return string_find_last_or(in_str, search_for, from, -1);
    }
    EXTERNAL isize string_find_first_char(String in_str, char search_for, isize from)
    {
        return string_find_first_char_or(in_str, search_for, from, -1);
    }
    EXTERNAL isize string_find_last_char(String in_str, char search_for, isize from)
    {
        return string_find_last_char_or(in_str, search_for, from, -1);
    }
    
    EXTERNAL int string_compare(String a, String b)
    {
        if(a.count > b.count)
            return -1;
        if(a.count < b.count)
            return 1;
        return memcmp(a.data, b.data, (size_t) a.count);
    }

    EXTERNAL int string_compare_lexicographic(String a, String b)
    {
        isize min_count = a.count < b.count ? a.count : b.count;
        int cmp = memcmp(a.data, b.data, (size_t) min_count);
        if(cmp != 0)
            return cmp;
        return (a.count > b.count) - (a.count < b.count);
    }
    
    EXTERNAL bool string_is_equal(String a, String b)
    {
        return a.count == b.count && memcmp(a.data, b.data, (size_t) a.count) == 0;
    }

    EXTERNAL bool string_is_equal_nocase(String a, String b)
    {
        if(a.count == b.count)
        {
            for(isize i = 0; i < a.count; i++)
                if(char_to_lower(a.data[i]) != char_to_lower(b.data[i]))
                    return false;
            return true;
        }
        return false;
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

    EXTERNAL bool string_has_substring_at(String string, String substring, isize at_index)
    {
        if(at_index < 0 || string.count - at_index < substring.count)
            return false;

        String portion = string_range(string, at_index, at_index + substring.count);
        return string_is_equal(portion, substring);
    }


    //String util functions ============================================    
    EXTERNAL isize string_null_terminate(char* buffer, isize buffer_size, String string)
    {
        isize min_size = 0;
        if(buffer_size > 0)
        {
            min_size = buffer_size - 1; 
            min_size = string.count < min_size ? string.count : min_size;
            memcpy(buffer, string.data, (size_t) min_size);
            buffer[min_size] = '\0';
        }
        return min_size;
    }

    EXTERNAL String string_allocate(Allocator* alloc, String string)
    {
        char* data = (char*) (*alloc)(alloc, 0, string.count + 1, NULL, 0, 1, NULL);
        memcpy(data, string.data, (size_t) string.count);
        data[string.count] = '\0';
        String out = {data, string.count};
        return out;
    }

    EXTERNAL void string_deallocate(Allocator* alloc, String* string)
    {
        if(string->data != NULL)
            (char*) (*alloc)(alloc, 0, 0, (void*) string->data, string->count + 1, 1, NULL);
        String nil = {0};
        *string = nil;
    }
    
    EXTERNAL void string_reallocate(Allocator* alloc, String* string, String new_val)
    {
        String old_val = *string;
        *string = string_allocate(alloc, new_val);
        string_deallocate(alloc, &old_val);
    }

    //Builder functions ========================================
    static char _builder_null_termination[64] = {0};
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

        //If is not in 0 state must be null terminated. 
        //In particular everything between count and capacity needs to be zero
        //This property is used to not have to add `\0` on pushes/appends
        bool is_null_terminated = true;
        if(builder.data != NULL) {
            for(isize i = builder.count; i <= builder.capacity; i++)
                if(builder.data[i] != '\0')
                    is_null_terminated = false;
        }
        
        bool result = is_capacity_correct && is_size_correct && is_data_correct && is_null_terminated && null_termination_not_corrupted;
        ASSERT(result);
        return result;
    }
    EXTERNAL void builder_deinit(String_Builder* builder)
    {
        ASSERT_SLOW(builder_is_invariant(*builder));
        if(builder->data != NULL && builder->data != _builder_null_termination)
            (*builder->allocator)(builder->allocator, 0, 0, builder->data, builder->capacity + 1, 1, NULL);
    
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
        //Keep in mind our allocator works as both alloc, realloc, and free all at once
        // so we make sure to call it only once for all cases
        if(builder->data != NULL && builder->data != _builder_null_termination)
        {
            ASSERT(builder->capacity > 0);
            old_data = builder->data;
            old_alloced = builder->capacity + 1;
        }
        if(capacity == 0)
            new_alloced = 0;

        builder->data = (char*) (*builder->allocator)(builder->allocator, 0, new_alloced, old_data, old_alloced, 1, NULL);
        
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

    EXTERNAL void builder_resize(String_Builder* builder, isize to_size, int fill_with_or_minus_one)
    {
        ASSERT_SLOW(builder_is_invariant(*builder));
        builder_reserve(builder, to_size);
        if(to_size >= builder->count)
        {
            if((uint32_t) fill_with_or_minus_one < 256)
                memset(builder->data + builder->count, fill_with_or_minus_one, (size_t) ((to_size - builder->count)));
            builder->data[builder->count] = '\0'; 
        }
        else
            //We clear the memory when shrinking so that we dont have to clear it when pushing!
            memset(builder->data + to_size, 0, (size_t) ((builder->count - to_size)));
        
        builder->count = to_size;
        ASSERT_SLOW(builder_is_invariant(*builder));
    }

    EXTERNAL void builder_clear(String_Builder* builder)
    {
        builder_resize(builder, 0, BUILDER_REISIZE_FOR_OVERWRITE);
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
        CHECK_BOUNDS(at, builder->count);
        builder_reserve(builder, builder->count + hole_size);
        memmove(builder->data + at + hole_size, builder->data + at, (size_t) hole_size);
        if((uint32_t) fill_with_char_or_minus_one < 256)
            memset(builder->data + at, fill_with_char_or_minus_one, (size_t) hole_size);

        builder->count += hole_size;
        ASSERT_SLOW(builder_is_invariant(*builder));
    }
    
    EXTERNAL void builder_insert(String_Builder* builder, isize at, String string)
    {
        builder_insert_hole(builder, at, string.count, BUILDER_REISIZE_FOR_OVERWRITE);
        memcpy(builder->data + at, string.data, (size_t) string.count);
        ASSERT_SLOW(builder_is_invariant(*builder));
    }

    EXTERNAL void builder_assign(String_Builder* builder, String string)
    {
        builder_resize(builder, string.count, BUILDER_REISIZE_FOR_OVERWRITE);
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
        CHECK_BOUNDS(0, builder->count);
        char popped = builder->data[--builder->count];
        builder->data[builder->count] = '\0';
        return popped;
    }
    
    EXTERNAL String_Builder builder_of(Allocator* allocator, String string)
    {
        String_Builder builder = builder_make(allocator, 0);
        if(string.count)
            builder_assign(&builder, string);
        return builder;
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

    //Formating functions ========================================
    #include <stdio.h>
    #include <stdarg.h>
    EXTERNAL void vformat_append_into(String_Builder* append_to, const char* format, va_list args)
    {
        PROFILE_START();
        if(format != NULL)
        {
            char local[1024];
            va_list args_copy;
            va_copy(args_copy, args);

            int size = vsnprintf(local, sizeof local, format, args_copy);
            isize base_size = append_to->count; 
            builder_resize(append_to, append_to->count + size, BUILDER_REISIZE_FOR_OVERWRITE);

            if(size > sizeof local) {
                PROFILE_INSTANT("format twice")
                vsnprintf(append_to->data + base_size, size + 1, format, args);
            }
            else
                memcpy(append_to->data + base_size, local, size);

        }
        PROFILE_STOP();
        ASSERT(builder_is_invariant(*append_to));
        return;
    }
    
    EXTERNAL void _format_append_into(String_Builder* append_to, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_append_into(append_to, format, args);
        va_end(args);
    }
    
    EXTERNAL void vformat_into(String_Builder* into, const char* format, va_list args)
    {
        builder_clear(into);
        vformat_append_into(into, format, args);
    }

    EXTERNAL void _format_into(String_Builder* into, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_into(into, format, args);
        va_end(args);
    }
    
    EXTERNAL String_Builder vformat(Allocator* alloc, const char* format, va_list args)
    {
        String_Builder builder = builder_make(alloc, 0);
        vformat_append_into(&builder, format, args);
        return builder;
    }
    
    EXTERNAL String_Builder _format(Allocator* alloc, const char* format, ...)
    {   
        va_list args;
        va_start(args, format);
        String_Builder builder = vformat(alloc, format, args);
        va_end(args);
        return builder;
    }


    EXTERNAL bool builder_is_equal(String_Builder a, String_Builder b)
    {
        return string_is_equal(a.string, b.string);
    }
    
    EXTERNAL int builder_compare(String_Builder a, String_Builder b)
    {
        return string_compare(a.string, b.string);
    }
    
    EXTERNAL int  string_compare_ptrs(const String* a, const String* b)
    {
        return string_compare(*a, *b);
    }
    EXTERNAL bool string_is_equal_ptrs(const String* a, const String* b)
    {
        return string_is_equal(*a, *b);
    }
    EXTERNAL int  builder_compare_ptrs(const String_Builder* a, const String_Builder* b)
    {
        return builder_compare(*a, *b);
    }
    EXTERNAL bool builder_is_equal_ptrs(const String_Builder* a, const String_Builder* b)
    {
        return builder_is_equal(*a, *b);
    }

    EXTERNAL bool char_is_space(char c)
    {
        return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
    }

    EXTERNAL bool char_is_digit(char c)
    {
        return '0' <= c && c <= '9';
    }

    EXTERNAL bool char_is_lower(char c)
    {
        return 'a' <= c && c <= 'z';
    }

    EXTERNAL bool char_is_upper(char c)
    {
        return 'A' <= c && c <= 'Z';
    }

    EXTERNAL bool char_is_alpha(char c)
    {
        //return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');

        //this is just a cute way of doing the two range checks in one
        //using the fact that all uppercase to lowercase letters are 32 apart.
        //That means we can just makes the fifth bit and test once.
        //Note that clang produces the same assembly for both the naive version above and this one.
        unsigned masked = (unsigned) (c - 'A') & ~(1u << 5);
        bool is_letter = masked <= 'Z' - 'A';

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
        if('A' <= c && c <= 'Z')
            return c - 'A' + 'a';
        else    
            return c;
    }
    
    EXTERNAL bool line_iterator_next(Line_Iterator* iterator, String string)
    {
        isize line_from = iterator->next_line_from;
        if(line_from >= string.count)
            return false;

        isize next_line_from = string.count;
        isize line_to = string.count;
        for(isize i = line_from; i < string.count; i++) {
            //linux style
            if(string.data[i] == '\n')
            {
                line_to = i;
                next_line_from = i+1;
                break;
            }
            if(string.data[i] == '\r')
            {
                //windows
                if(i + 1 < string.count && string.data[i] == '\n')
                    next_line_from = i+2;
                //mac
                else
                    next_line_from = i+1;

                line_to = i;
                break;
            }
        }
        
        if(next_line_from > string.count)
            next_line_from = string.count;

        iterator->line_number += 1;
        iterator->line_from = line_from;
        iterator->line_to = line_to;
        iterator->next_line_from = next_line_from;
        iterator->line = string_range(string, line_from, line_to);
        return true;
    }

    EXTERNAL String string_trim_prefix_whitespace(String s)
    {
        isize from = 0;
        for(; from < s.count; from++)
            if(char_is_space(s.data[from]) == false)
                break;

        return string_tail(s, from);
    }
    EXTERNAL String string_trim_postfix_whitespace(String s)
    {
        isize to = s.count;
        for(; to-- > 0; )
            if(char_is_space(s.data[to]) == false)
                break;

        return string_head(s, to + 1);
    }
    EXTERNAL String string_trim_whitespace(String s)
    {
        String prefix_trimmed = string_trim_prefix_whitespace(s);
        String both_trimmed = string_trim_postfix_whitespace(prefix_trimmed);

        return both_trimmed;
    }
#endif