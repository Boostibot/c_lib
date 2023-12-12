#ifndef JOT_STRING
#define JOT_STRING

#include "allocator.h"
#include "array.h"

typedef Platform_String String;

DEFINE_ARRAY_TYPE(char, String_Builder);
DEFINE_ARRAY_TYPE(String, String_Array);
DEFINE_ARRAY_TYPE(String_Builder, String_Builder_Array);

//Constructs a String out of a string literal. Cannot be used with dynamic strings!
#define STRING(cstring) BRACE_INIT(String){cstring, sizeof(cstring) - 1}

//if the string is valid -> returns it
//if the string is NULL  -> returns ""
EXPORT const char*      cstring_escape(const char* string);
//Returns always null terminated string contained within a builder
EXPORT const char*      cstring_from_builder(String_Builder builder); 
//if string is NULL returns 0 else strlen(string)
EXPORT isize safe_strlen(const char* string);

//Returns a String contained within string builder. The data portion of the string MIGHT be null and in that case its size == 0
EXPORT String string_from_builder(String_Builder builder); 
EXPORT String string_make(const char* cstring); //converts a null terminated cstring into a String
EXPORT String string_head(String string, isize to); //keeps only charcters to to ( [0, to) interval )
EXPORT String string_tail(String string, isize from); //keeps only charcters from from ( [from, string.size) interval )
EXPORT String string_safe_head(String string, isize to); //returns string_head using to. If to is outside the range [0, string.size] clamps it to the range. 
EXPORT String string_safe_tail(String string, isize from); //returns string_tail using from. If from is outside the range [0, string.size] clamps it to the range. 
EXPORT String string_range(String string, isize from, isize to); //returns a string containing characters staring from from and ending in to ( [from, to) interval )
EXPORT String string_portion(String string, isize from, isize size); //returns a string containing size characters staring from ( [from, from + size) interval )
EXPORT bool   string_is_equal(String a, String b); //Returns true if the contents and sizes of the strings match
EXPORT bool   string_is_prefixed_with(String string, String prefix); 
EXPORT bool   string_is_postfixed_with(String string, String postfix);
EXPORT bool   string_has_substring_at(String larger_string, isize from_index, String smaller_string); //Retursn true if larger_string has smaller_string at index from_index
EXPORT int    string_compare(String a, String b); //Compares sizes and then lexographically the contents. Shorter strings are placed before longer ones.
EXPORT isize  string_find_first(String string, String search_for, isize from); 

EXPORT isize  string_find_last_from(String in_str, String search_for, isize from);
EXPORT isize  string_find_last(String string, String search_for); 

EXPORT isize  string_find_first_char(String string, char search_for, isize from); 
EXPORT isize  string_find_first_char_vanilla(String string, char search_for, isize from); 
EXPORT isize  string_find_first_char_far(String string, char search_for, isize from);
EXPORT isize  string_find_first_char_far_unsafe(String string, char search_for, isize from);
EXPORT isize  string_find_last_char_from(String in_str, char search_for, isize from);
EXPORT isize  string_find_last_char(String string, char search_for); 

EXPORT void             builder_append(String_Builder* builder, String string); //Appends a string
EXPORT void             builder_assign(String_Builder* builder, String string); //Sets the contents of the builder to be equal to string
EXPORT String_Builder   builder_from_cstring(const char* cstring, Allocator* allocator); //Allocates a String_Builder from cstring. The String_Builder needs to be deinit just line any other ???_Array type!
EXPORT String_Builder   builder_from_string(String string, Allocator* allocator);  //Allocates a String_Builder from String using an allocator. The String_Builder needs to be deinit just line any other ???_Array type!
EXPORT void             builder_array_deinit(String_Builder_Array* array);

EXPORT bool             builder_is_equal(String_Builder a, String_Builder b); //Returns true if the contents and sizes of the strings match
EXPORT int              builder_compare(String_Builder a, String_Builder b); //Compares sizes and then lexographically the contents. Shorter strings are placed before longer ones.

EXPORT void string_join_into(String_Builder* append_to, const String* strings, isize strings_count, String separator); //Appends all strings in the strings array to append_to
EXPORT void string_split_into(String_Array* append_to, String to_split, String split_by); //Splits the to_split string using split_by as a separator and appends the individual split parts into append_to

EXPORT String_Builder string_join(String a, String b); //Allocates a new String_Builder with the concatenated String's a and b
EXPORT String_Builder cstring_join(const char* a, const char* b); //Allocates a new String_Builder with the concatenated cstring's a and b
EXPORT String_Builder string_join_any(const String* strings, isize strings_count, String separator); 
EXPORT String_Array string_split(String to_split, String split_by);

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

    EXPORT String string_safe_head(String string, isize to)
    {
        return string_head(string, CLAMP(to, 0, string.size));
    }
    
    EXPORT String string_safe_tail(String string, isize from)
    {
        return string_tail(string, CLAMP(from, 0, string.size));
    }

    EXPORT String string_range(String string, isize from, isize to)
    {
        return string_tail(string_head(string, to), from);
    }

    EXPORT String string_portion(String string, isize from, isize size)
    {
        return string_head(string_tail(string, from), size);
    }

    EXPORT String string_make(const char* cstring)
    {
        String out = {cstring, safe_strlen(cstring)};
        return out;
    }

    EXPORT isize string_find_first(String in_str, String search_for, isize from)
    {
        CHECK_BOUNDS(from, in_str.size);
        if(search_for.size == 0)
            return from;
        
        ASSERT(from >= 0);
        isize to = in_str.size - search_for.size + 1;
        for(isize i = from; i < to; i++)
        {
            bool found = true;
            for(isize j = 0; j < search_for.size; j++)
            {
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
      
    EXPORT isize string_find_last_from(String in_str, String search_for, isize from)
    {
        CHECK_BOUNDS(from, in_str.size);
        if(search_for.size == 0)
            return from;

        ASSERT(false && "UNTESTED!");
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
        ASSERT(from >= 0);
        isize found_unsafe = string_find_first_char_far_unsafe(string, search_for, from);
        #if 1
        isize found_vanilal = string_find_first_char_vanilla(string, search_for, from);
        ASSERT(found_vanilal == found_unsafe);
        #endif

        return found_unsafe;
    }
    
    EXPORT isize string_find_first_char_far(String string, char search_for, isize from)
    {
        bool is_big_endian = false;
        #ifdef PLATFORM_HAS_ENDIAN_BIG
        is_big_endian = true;
        #endif

        if(is_big_endian)
            return string_find_first_char_vanilla(string, search_for, from);

        const char* search_start = string.data + from;
        const char* string_end = string.data + string.size;
        const char* start_of_long_search = (const char*) align_forward((void*) search_start, 8);
        const char* end_of_long_search = (const char*) align_backward((void*) string_end, 8);
        end_of_long_search = MAX(end_of_long_search, start_of_long_search);

        #define broadcast64(c) ((u64) 0x0101010101010101ULL * (u64) c)
        #define haszero64(v) (((v) - (u64) 0x0101010101010101ULL) & ~(v) & (u64) 0x8080808080808080ULL)
        
        u64 search_mask = broadcast64(search_for);

        //@TODO: We could get rid of the unaligned loops because pages are always 8 aligned 
        //       thus reading them can never cause page fault. This could make this function mutch smaller in 
        //       assembly

        for(const char* i = search_start; i < start_of_long_search; i++)
            if(*i == search_for)
                return (isize) (i - string.data);

        for(const char* i = start_of_long_search; i < end_of_long_search; i += 8)
        {
            u64 chunk = *(const u64*) i; 
            u64 masked = chunk ^ search_mask;
            if(haszero64(masked))
            {
                if(i[0] == search_for) return (isize) (i + 0 - string.data);
                if(i[1] == search_for) return (isize) (i + 1 - string.data);
                if(i[2] == search_for) return (isize) (i + 2 - string.data);
                if(i[3] == search_for) return (isize) (i + 3 - string.data);
                if(i[4] == search_for) return (isize) (i + 4 - string.data);
                if(i[5] == search_for) return (isize) (i + 5 - string.data);
                if(i[6] == search_for) return (isize) (i + 6 - string.data);

                return (isize) (i + 7 - string.data);
            }
        }

        for(const char* i = end_of_long_search; i < string_end; i++)
            if(*i == search_for)
                return (isize) (i - string.data);
    
        return -1;



    }
    
    EXPORT isize string_find_first_char_vanilla(String string, char search_for, isize from)
    {
        ASSERT(from >= 0);
        for(isize i = from; i < string.size; i++)
            if(string.data[i] == search_for)
                return i;

        return -1;
    }

    EXPORT isize string_find_first_char_far_unsafe(String string, char search_for, isize from)
    {
        //This is a very fast (not the fastest since it doesnt use SIMD) string find implementation.
        //The idea is to scan 8 characters simulatneously using clever bit hacks and then localize
        //the exact place of the match.
        // 
        //The chunks need to be 64 aligned so that we can read them as normal u64. We can however
        //skip the code that would find within the reagion before and after the 64 aligned chunks.
        //We do this by simply reading OUTSIDE THE BOUNDS OF THE ARRAY. This is okay since we can
        //never trigger a page fault (pages are 64 aligned) and we shouldnt cause any debugger issues
        //since we are only reading the adresses.

        bool is_big_endian = false;
        #ifdef PLATFORM_BIG_ENDIAN
        is_big_endian = true;
        #endif

        if(is_big_endian)
            return string_find_first_char_vanilla(string, search_for, from);

        ASSERT(from >= 0);
        if(string.size == 0 || from >= string.size)
            return -1;

        const char* search_start = string.data + from;
        const char* string_end = string.data + string.size;
        const char* long_search_start = (const char*) align_backward((void*) search_start, 8);
        isize overread_before = search_start - long_search_start;

        #define broadcast64(c) ((u64) 0x0101010101010101ULL * (u64) c)
        #define haszero64(v) (((v) - (u64) 0x0101010101010101ULL) & ~(v) & (u64) 0x8080808080808080ULL)
        
        const char* i = long_search_start;
        u64 search_mask = broadcast64(search_for);
        u64 first_chunk = *(const u64*) i;
        u64 overread_mask = ~((~(u64) 0) << (overread_before * 8)); //masks off matches before the valid data of the string
        u64 masked = (first_chunk ^ search_mask) | overread_mask;
        
        if(haszero64(masked))
            goto localize_match;

        for(i += 8; i < string_end; i += 8)
        {
            u64 chunk = *(const u64*) i; 
            masked = chunk ^ search_mask;
            if(haszero64(masked))
                goto localize_match;
        }

        return -1;
        localize_match: {
            isize found = 7;
            
            if(((masked >> 0*8) & 0xff) == 0) { found = 0; goto function_end; }
            if(((masked >> 1*8) & 0xff) == 0) { found = 1; goto function_end; }
            if(((masked >> 2*8) & 0xff) == 0) { found = 2; goto function_end; }
            if(((masked >> 3*8) & 0xff) == 0) { found = 3; goto function_end; }
            if(((masked >> 4*8) & 0xff) == 0) { found = 4; goto function_end; }
            if(((masked >> 5*8) & 0xff) == 0) { found = 5; goto function_end; }
            if(((masked >> 6*8) & 0xff) == 0) { found = 6; goto function_end; }

            function_end:

            found += i - string.data;
            if(string.data + found >= string_end)
                found = -1;

            return found;
        }

        #undef broadcast64
        #undef haszero64
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

        String portion = string_portion(larger_string, from_index, smaller_string.size);
        return string_is_equal(portion, smaller_string);
    }

    EXPORT const char* cstring_escape(const char* string)
    {
        if(string == NULL)
            return "";
        else
            return string;
    }
    
    EXPORT isize safe_strlen(const char* string)
    {
        if(string == NULL)
            return 0;
        else
            return (isize) strlen(string);
    }

    EXPORT const char* cstring_from_builder(String_Builder builder)
    {
        return cstring_escape(builder.data);
    }

    EXPORT String string_from_builder(String_Builder builder)
    {
        String out = {builder.data, builder.size};
        return out;
    }
    
    EXPORT void builder_append(String_Builder* builder, String string)
    {
        array_append(builder, string.data, string.size);
    }

    EXPORT void builder_assign(String_Builder* builder, String string)
    {
        array_assign(builder, string.data, string.size);
    }
    
    EXPORT void builder_array_deinit(String_Builder_Array* array)
    {
        for(isize i = 0; i < array->size; i++)
            array_deinit(&array->data[i]);

        array_deinit(array);
    }

    EXPORT String_Builder builder_from_string(String string, Allocator* allocator)
    {
        String_Builder builder = {allocator};
        array_append(&builder, string.data, string.size);
        return builder;
    }

    EXPORT String_Builder builder_from_cstring(const char* cstring, Allocator* allocator)
    {
        return builder_from_string(string_make(cstring), allocator);
    }

    EXPORT bool builder_is_equal(String_Builder a, String_Builder b)
    {
        return string_is_equal(string_from_builder(a), string_from_builder(b));
    }
    
    EXPORT int builder_compare(String_Builder a, String_Builder b)
    {
        return string_compare(string_from_builder(a), string_from_builder(b));
    }

    EXPORT void string_join_into(String_Builder* append_to, const String* strings, isize strings_count, String separator)
    {
        if(strings_count == 0)
            return;

        isize size_sum = 0;
        for(isize i = 0; i < strings_count; i++)
            size_sum += strings[i].size;

        size_sum += separator.size * (strings_count - 1);

        array_reserve(append_to, append_to->size + size_sum);
        builder_append(append_to, strings[0]);

        for(isize i = 1; i < strings_count; i++)
        {
            builder_append(append_to, separator);
            builder_append(append_to, strings[i]);
        }
    }

    EXPORT void string_split_into(String_Array* parts, String to_split, String split_by)
    {
        isize from = 0;
        for(isize i = 0; i < to_split.size; i++)
        {
            isize to = string_find_first(to_split, split_by, from);
            if(to == -1)
            {
                String part = string_range(to_split, from, to_split.size);
                array_push(parts, part);
                break;
            }

            String part = string_range(to_split, from, to);
            array_push(parts, part);
            from = to + split_by.size;
        }
    }

    EXPORT String_Builder string_join(String a, String b)
    {
        String_Builder joined = {0};
        array_resize(&joined, a.size + b.size);
        ASSERT(joined.data != NULL);
        memcpy(joined.data, a.data, (size_t) a.size);
        memcpy(joined.data + a.size, b.data, (size_t) b.size);

        return joined;
    }

    EXPORT String_Builder cstring_join(const char* a, const char* b)
    {
        return string_join(string_make(a), string_make(b));
    }

    EXPORT String_Builder string_join_any(const String* strings, isize strings_count, String separator)
    {
        String_Builder builder = {0};
        string_join_into(&builder, strings, strings_count, separator);
        return builder;
    }
    EXPORT String_Array string_split(String to_split, String split_by)
    {
        String_Array parts = {0};
        string_split_into(&parts, to_split, split_by);
        return parts;
    }
#endif