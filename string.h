#ifndef JOT_STRING
#define JOT_STRING

#include "allocator.h"
#include "array.h"

typedef Platform_String String;

//A dynamically resizeable string. Its data member is always null 
// terminated even without any allocations. 
// (as long as it was properly initialized - ie. not in = {0} state)
typedef struct String_Builder {
    Allocator* allocator;
    isize capacity;
    //A slightly weird construction so that we can easily 
    // obtain a string from the string builder.
    //This prevents us from constantly havign to type
    //  string_from_builder(builder)
    // and instead just
    //  builder.string
    union {
        struct {
            char* data;
            isize size;
        };

        String string;
    };
} String_Builder;

typedef Array(String) String_Array;
typedef Array(String_Builder) String_Builder_Array;

//Constructs a String out of a string literal
#define STRING(cstring) BRACE_INIT(String){cstring "", sizeof(cstring) - 1}

//if the string is valid -> returns it
//if the string is NULL  -> returns ""
EXPORT const char*      cstring_escape(const char* string);
//if string is NULL returns 0 else strlen(string)
EXPORT isize safe_strlen(const char* string, isize max_size_or_minus_one);

//Tiles pattern_size bytes long pattern across field of field_size bytes. 
//The first occurance of pattern is placed at the very start of field and subsequent repetions
//follow. 
//If the field_size % pattern_size != 0 the last repetition of pattern is trimmed.
//If pattern_size == 0 field is filled with zeros instead.
EXPORT void memset_pattern(void *field, isize field_size, const void* pattern, isize pattern_size);

#define _string_make_internal(string, size, ...) (BRACE_INIT(String){(string), (size)})
//Makes a string. Either call like string_make("hello world") in which case strlen of the passed in string is used or 
// string_make(my_data, my_size) in which case the passed in 'my_size' size is used
#define string_make(string, ...) _string_make_internal((string), ##__VA_ARGS__, (isize) strlen(string)) 

EXPORT String string_head(String string, isize to); //keeps only charcters to to ( [0, to) interval )
EXPORT String string_tail(String string, isize from); //keeps only charcters from from ( [from, string.size) interval )
EXPORT String string_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXPORT String string_safe_head(String string, isize to); //returns string_head using to. If to is outside the range [0, string.size] clamps it to the range. 
EXPORT String string_safe_tail(String string, isize from); //returns string_tail using from. If from is outside the range [0, string.size] clamps it to the range. 
EXPORT String string_safe_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXPORT bool   string_is_equal(String a, String b); //Returns true if the contents and sizes of the strings match
EXPORT bool   string_is_prefixed_with(String string, String prefix); 
EXPORT bool   string_is_postfixed_with(String string, String postfix);
EXPORT bool   string_has_substring_at(String larger_string, isize from_index, String smaller_string); //Retursn true if larger_string has smaller_string at index from_index
EXPORT int    string_compare(String a, String b); //Compares sizes and then lexographically the contents. Shorter strings are placed before longer ones.

EXPORT isize  string_find_first(String string, String search_for, isize from); 
EXPORT isize  string_find_last_from(String in_str, String search_for, isize from);
EXPORT isize  string_find_last(String string, String search_for); 

EXPORT isize  string_find_first_char(String string, char search_for, isize from); 
EXPORT isize  string_find_last_char_from(String in_str, char search_for, isize from);
EXPORT isize  string_find_last_char(String string, char search_for); 

EXPORT void string_deallocate(Allocator* arena, String* string);

EXPORT String_Builder builder_make(Allocator* alloc_or_null, isize capacity_or_zero);
EXPORT String_Builder builder_from_cstring(Allocator* allocator, const char* cstring); //Allocates a String_Builder from cstring.
EXPORT String_Builder builder_from_string(Allocator* allocator, String string);  //Allocates a String_Builder from String using an allocator.

EXPORT String_Builder string_concat(Allocator* allocator, String a, String b);
EXPORT String_Builder string_concat3(Allocator* allocator, String a, String b, String c);

EXPORT String_Builder* string_ephemeral_select(String_Builder* ephemerals, isize* slot, isize slot_count, isize min_size, isize max_size, isize reset_every);

EXPORT void builder_init(String_Builder* builder, Allocator* alloc);
EXPORT void builder_init_with_capacity(String_Builder* builder, Allocator* alloc, isize capacity_or_zero);
EXPORT void builder_deinit(String_Builder* builder);             
EXPORT void builder_set_capacity(String_Builder* builder, isize capacity);             
EXPORT void builder_resize(String_Builder* builder, isize capacity);             
EXPORT void builder_clear(String_Builder* builder);             
EXPORT void builder_push(String_Builder* builder, char c);             
EXPORT char builder_pop(String_Builder* builder);             
EXPORT void builder_append(String_Builder* builder, String string); //Appends a string
EXPORT void builder_assign(String_Builder* builder, String string); //Sets the contents of the builder to be equal to string
EXPORT bool builder_is_equal(String_Builder a, String_Builder b); //Returns true if the contents and sizes of the strings match
EXPORT int  builder_compare(String_Builder a, String_Builder b); //Compares sizes and then lexographically the contents. Shorter strings are placed before longer ones.

EXPORT void builder_array_deinit(String_Builder_Array* array);

//Replaces in source that are equal to some character from to_replace with the character at the same exact position of replace_with.
//If there is '\0' at the matching position of replace_with, removes the character without substituting"
//So string_replace(..., "Hello world", "lw", ".\0") -> "He..o or.d"
EXPORT String_Builder string_replace(Allocator* allocator, String source, String to_replace, String replace_with);

EXPORT bool char_is_space(char c);
EXPORT bool char_is_digit(char c);
EXPORT bool char_is_lowercase(char c);
EXPORT bool char_is_uppercase(char c);
EXPORT bool char_is_alphabetic(char c);
EXPORT bool char_is_id(char c);


#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_STRING_IMPL)) && !defined(JOT_STRING_HAS_IMPL)
#define JOT_STRING_HAS_IMPL

    #include <string.h>
    EXPORT String string_head(String string, isize to)
    {
        CHECK_BOUNDS(to, string.size + 1);
        String head = {string.data, to};
        return head;
    }

    EXPORT String string_tail(String string, isize from)
    {
        CHECK_BOUNDS(from, string.size + 1);
        String tail = {string.data + from, string.size - from};
        return tail;
    }
    
    EXPORT String string_range(String string, isize from, isize to)
    {
        return string_tail(string_head(string, to), from);
    }

    EXPORT String string_safe_head(String string, isize to)
    {
        return string_head(string, CLAMP(to, 0, string.size));
    }
    
    EXPORT String string_safe_tail(String string, isize from)
    {
        return string_tail(string, CLAMP(from, 0, string.size));
    }

    EXPORT String string_safe_range(String string, isize from, isize to)
    {
        isize escaped_from = CLAMP(from, 0, string.size);
        isize escaped_to = CLAMP(to, 0, string.size);
        return string_range(string, escaped_from, escaped_to);
    }

    EXPORT isize string_find_first(String in_str, String search_for, isize from)
    {
        ASSERT(from >= 0);

        if(from + search_for.size > in_str.size)
            return -1;
        
        if(search_for.size == 0)
            return from;

        if(search_for.size == 1)
            return string_find_first_char(in_str, search_for.data[0], from);

        const char* found = in_str.data + from;
        char last_char = search_for.data[search_for.size - 1];
        char first_char = search_for.data[0];

        while (true)
        {
            isize remaining_length = in_str.size - (found - in_str.data) - search_for.size + 1;
            ASSERT(remaining_length >= 0);

            found = (const char*) memchr(found, first_char, remaining_length);
            if(found == NULL)
                return -1;
                
            char last_char_of_found = found[search_for.size - 1];
            if (last_char_of_found == last_char)
                if (memcmp(found + 1, search_for.data + 1, search_for.size - 2) == 0)
                    return found - in_str.data;

            found += 1;
        }

        return -1;
    }
      
    EXPORT isize string_find_last_from(String in_str, String search_for, isize from)
    {
        ASSERT(from >= 0);
        if(from + search_for.size > in_str.size)
            return -1;

        if(search_for.size == 0)
            return from;

        ASSERT(false, "UNTESTED! @TODO: test!");
        ASSERT(from >= 0);
        isize start = from;
        if(in_str.size - start < search_for.size)
            start = in_str.size - search_for.size;

        for(isize i = start; i-- > 0; )
        {
            bool found = true;
            for(isize j = 0; j < search_for.size; j++)
            {
                CHECK_BOUNDS(i + j, in_str.size);
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

    EXPORT isize string_find_last(String in_str, String search_for)
    {
        isize from = MAX(in_str.size - 1, 0);
        return string_find_last_from(in_str, search_for, from);
    }
    
    EXPORT isize string_find_first_char(String string, char search_for, isize from)
    {
        char* ptr = (char*) memchr(string.data + from, search_for, string.size - from);
        return ptr ? (isize) (ptr - string.data) : -1; 
    }
    
    EXPORT void memset_pattern(void *field, isize field_size, const void* pattern, isize pattern_size)
    {
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
    }

    EXPORT isize string_find_last_char_from(String string, char search_for, isize from)
    {
        for(isize i = from + 1; i-- > 0; )
            if(string.data[i] == search_for)
                return i;

        return -1;
    }
    
    EXPORT isize string_find_last_char(String string, char search_for)
    {
        return string_find_last_char_from(string, search_for, string.size - 1);
    }

    EXPORT int string_compare(String a, String b)
    {
        if(a.size > b.size)
            return -1;
        if(a.size < b.size)
            return 1;

        int res = memcmp(a.data, b.data, (u64) a.size);
        return res;
    }
    
    EXPORT bool string_is_equal(String a, String b)
    {
        if(a.size != b.size)
            return false;

        bool eq = memcmp(a.data, b.data, (u64) a.size) == 0;
        return eq;
    }

    EXPORT bool string_is_prefixed_with(String string, String prefix)
    {
        if(string.size < prefix.size)
            return false;

        String trimmed = string_head(string, prefix.size);
        return string_is_equal(trimmed, prefix);
    }

    EXPORT bool string_is_postfixed_with(String string, String postfix)
    {
        if(string.size < postfix.size)
            return false;

        String trimmed = string_tail(string, postfix.size);
        return string_is_equal(trimmed, postfix);
    }

    EXPORT bool string_has_substring_at(String larger_string, isize from_index, String smaller_string)
    {
        if(larger_string.size - from_index < smaller_string.size)
            return false;

        String portion = string_range(larger_string, from_index, from_index + smaller_string.size);
        return string_is_equal(portion, smaller_string);
    }
    
    EXPORT String_Builder string_concat(Allocator* allocator, String a, String b)
    {
        String_Builder out = builder_make(allocator, a.size + b.size);
        builder_append(&out, a);
        builder_append(&out, b);
        return out;
    }

    EXPORT String_Builder string_concat3(Allocator* allocator, String a, String b, String c)
    {
        String_Builder out = builder_make(allocator, a.size + b.size + c.size);
        builder_append(&out, a);
        builder_append(&out, b);
        builder_append(&out, c);
        return out;
    }
    
    EXPORT String_Builder* string_ephemeral_select(String_Builder* ephemerals, isize* slot, isize slot_count, isize min_size, isize max_size, isize reset_every)
    {
        String_Builder* curr = &ephemerals[*slot % slot_count];
        
        //We periodacally shrink the strinks so that we can use this
        //function regulary for small and big strings without fearing that we will
        //use too much memory
        if(*slot % reset_every < slot_count)
        {
            if(curr->capacity == 0 || curr->capacity > max_size)
                builder_init_with_capacity(curr, allocator_get_static(), min_size);
        }
        
        *slot += 1;
        return curr;
    }

    EXPORT void string_deallocate(Allocator* alloc, String* string)
    {
        if(string->size != 0)
            allocator_deallocate(alloc, (void*) string->data, string->size + 1, 1);
        String nil = {0};
        *string = nil;
    }

    EXPORT const char* cstring_escape(const char* string)
    {
        if(string == NULL)
            return "";
        else
            return string;
    }

    EXPORT isize safe_strlen(const char* string, isize max_size_or_minus_one)
    {
        if(string == NULL)
            return 0;
        if(max_size_or_minus_one < 0)
            max_size_or_minus_one = INT64_MAX;

        String max_string = {string, max_size_or_minus_one};
        return string_find_first_char(max_string, '\0', 0);
    }
    
    EXPORT const char* cstring_from_builder(String_Builder builder)
    {
        return cstring_escape(builder.data);
    }

    char _builder_null_termination[4] = {0};
    EXPORT bool _builder_is_invariant(const String_Builder* builder)
    {
        bool is_capacity_correct = 0 <= builder->capacity;
        bool is_size_correct = (0 <= builder->size && builder->size <= builder->capacity);
        //Data is default iff capacity is zero
        bool is_data_correct = (builder->data == NULL || builder->data == _builder_null_termination) == (builder->capacity == 0);

        //If has capacity then was allocated therefore must have an allocator set
        if(builder->capacity > 0)
            is_capacity_correct = is_capacity_correct && builder->allocator != NULL;

        //If is not in 0 state must be null terminated (both right after and after the whole capacity for safety)
        bool is_null_terminated = true;
        if(builder->data != NULL)
            is_null_terminated = builder->data[builder->size] == '\0' && builder->data[builder->capacity] == '\0';
        
        bool result = is_capacity_correct && is_size_correct && is_data_correct && is_null_terminated;
        ASSERT(result);
        return result;
    }
    EXPORT void builder_deinit(String_Builder* builder)
    {
        ASSERT(builder != NULL);
        ASSERT(_builder_is_invariant(builder));

        if(builder->data != NULL && builder->data != _builder_null_termination)
            allocator_deallocate(builder->allocator, builder->data, builder->capacity + 1, 1);
    
        memset(builder, 0, sizeof *builder);
    }
    
    EXPORT void builder_init(String_Builder* builder, Allocator* allocator)
    {
        builder_deinit(builder);
        builder->allocator = allocator;
        builder->data = _builder_null_termination;
        if(builder->allocator == NULL)
            builder->allocator = allocator_get_default();
    }

    EXPORT void builder_init_with_capacity(String_Builder* builder, Allocator* allocator, isize capacity_or_zero)
    {
        builder_init(builder, allocator);
        if(capacity_or_zero > 0)
            builder_set_capacity(builder, capacity_or_zero);
    }

    EXPORT String_Builder builder_make(Allocator* alloc_or_null, isize capacity_or_zero)
    {
        String_Builder builder = {0};
        builder.allocator = alloc_or_null;
        builder.data = _builder_null_termination;
        if(capacity_or_zero > 0)
            builder_set_capacity(&builder, capacity_or_zero);
        return builder;
    }

    EXPORT void builder_set_capacity(String_Builder* builder, isize capacity)
    {
        ASSERT(_builder_is_invariant(builder));
        ASSERT(capacity >= 0);

        //@TEMP: just for transition
        //ASSERT(builder->data != NULL);

        //If allocator is not set grab one from the context
        if(builder->allocator == NULL)
            builder->allocator = allocator_get_default();

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
        if(builder->size > builder->capacity)
            builder->size = builder->capacity;
        
        //Restore null termination
        if(capacity == 0)
            builder->data = _builder_null_termination;
        else
        {
            builder->data[builder->size] = '\0'; 
            builder->data[builder->capacity] = '\0'; 
        }
        ASSERT(_builder_is_invariant(builder));
    }
    
    EXPORT void builder_reserve(String_Builder* builder, isize to_fit)
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
    EXPORT void builder_resize(String_Builder* builder, isize to_size)
    {
        builder_reserve(builder, to_size);
        if(to_size >= builder->size)
            memset(builder->data + builder->size, 0, (size_t) ((to_size - builder->size)));
        else
            //We clear the memory when shrinking so that we dont have to clear it when pushing!
            memset(builder->data + to_size, 0, (size_t) ((builder->size - to_size)));
        
        builder->size = to_size;
        ASSERT(_builder_is_invariant(builder));
    }

    EXPORT void builder_clear(String_Builder* builder)
    {
        builder_resize(builder, 0);
    }

    EXPORT void builder_append(String_Builder* builder, String string)
    {
        ASSERT(string.size >= 0);
        builder_reserve(builder, builder->size+string.size);
        memcpy(builder->data + builder->size, string.data, (size_t) string.size);
        builder->size += string.size;
        ASSERT(_builder_is_invariant(builder));
    }

    EXPORT void builder_assign(String_Builder* builder, String string)
    {
        builder_resize(builder, string.size);
        memcpy(builder->data, string.data, (size_t) string.size);
        ASSERT(_builder_is_invariant(builder));
    }

    EXPORT void builder_push(String_Builder* builder, char c)
    {
        builder_reserve(builder, builder->size+1);
        builder->data[builder->size++] = c;
    }

    EXPORT char builder_pop(String_Builder* builder)
    {
        ASSERT(builder->size > 0);
        char popped = builder->data[--builder->size];
        builder->data[builder->size] = '\0';
        return popped;
    }
    
    EXPORT void builder_array_deinit(String_Builder_Array* array)
    {
        for(isize i = 0; i < array->size; i++)
            builder_deinit(&array->data[i]);

        array_deinit(array);
    }
    
    EXPORT String_Builder builder_from_string(Allocator* allocator, String string)
    {
        String_Builder builder = builder_make(allocator, string.size);
        if(string.size)
            builder_assign(&builder, string);
        return builder;
    }

    EXPORT String_Builder builder_from_cstring(Allocator* allocator, const char* cstring)
    {
        return builder_from_string(allocator, string_make(cstring));
    }

    EXPORT bool builder_is_equal(String_Builder a, String_Builder b)
    {
        return string_is_equal(a.string, b.string);
    }
    
    EXPORT int builder_compare(String_Builder a, String_Builder b)
    {
        return string_compare(a.string, b.string);
    }

    EXPORT bool char_is_space(char c)
    {
        switch(c)
        {
            case ' ':
            case '\n':
            case '\t':
            case '\r':
            case '\v':
            case '\f':
                return true;
            default: 
                return false;
        }
    }

    EXPORT bool char_is_digit(char c)
    {
        return '0' <= c && c <= '9';
    }

    EXPORT bool char_is_lowercase(char c)
    {
        return 'a' <= c && c <= 'z';
    }

    EXPORT bool char_is_uppercase(char c)
    {
        return 'A' <= c && c <= 'Z';
    }

    EXPORT bool char_is_alphabetic(char c)
    {
        //return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');

        //this is just a little flex of doing the two range checks in one
        //using the fact that all uppercase to lowercase leters are 32 appart.
        //That means we can just maks the fift bit and test once.
        //You can simply test this works by comparing the result of both approaches on all char values.
        char diff = c - 'A';
        char masked = diff & ~(1 << 5);
        bool is_letter = 0 <= masked && masked <= ('Z' - 'A');

        return is_letter;
    }

    //all characters permitted inside a common programming language id. [0-9], _, [a-z], [A-Z]
    EXPORT bool char_is_id(char c)
    {
        return char_is_digit(c) || char_is_alphabetic(c) || c == '_';
    }
#endif