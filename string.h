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
    //A slightly weird construction so that we can easily 
    // obtain a string from the string builder.
    //This prevents us from constantly ahvign to type
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
    isize capacity;
} String_Builder;

DEFINE_ARRAY_TYPE(String, String_Array);
DEFINE_ARRAY_TYPE(String_Builder, String_Builder_Array);

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

//Returns a String contained within string builder. The data portion of the string MIGHT be null and in that case its size == 0
//EXPORT String string_from_builder(String_Builder builder); 
EXPORT String string_make(const char* cstring); //converts a null terminated cstring into a String
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
EXPORT isize  string_find_first_char_vanilla(String string, char search_for, isize from); 
EXPORT isize  string_find_first_char_unsafe(String string, char search_for, isize from);
EXPORT isize  string_find_first_char_sse(String string, char search_for, isize from);
EXPORT isize  string_find_last_char_from(String in_str, char search_for, isize from);
EXPORT isize  string_find_last_char(String string, char search_for); 

EXPORT String string_duplicate(Arena* arena, String string);

EXPORT String_Builder builder_make(Allocator* alloc_or_null, isize capacity_or_zero);
EXPORT String_Builder builder_from_cstring(const char* cstring, Allocator* allocator); //Allocates a String_Builder from cstring.
EXPORT String_Builder builder_from_string(String string, Allocator* allocator);  //Allocates a String_Builder from String using an allocator.

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

    EXPORT String string_make(const char* cstring)
    {
        String out = {cstring, safe_strlen(cstring, -1)};
        return out;
    }

    EXPORT isize string_find_first(String in_str, String search_for, isize from)
    {
        ASSERT(from >= 0);

        //if there is not enough space for any occurence we return not found.
        if(from + search_for.size > in_str.size)
            return -1;
        
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
        ASSERT(from >= 0);
        isize found_unsafe = string_find_first_char_sse(string, search_for, from);
        #ifndef NDEBUG
        isize found_vanila = string_find_first_char_vanilla(string, search_for, from);
        if(found_vanila != found_unsafe)
        {
            platform_debug_break();
            found_unsafe = string_find_first_char_sse(string, search_for, from);
            found_vanila = string_find_first_char_vanilla(string, search_for, from);
        }
        ASSERT(found_vanila == found_unsafe);
        #endif

        return found_unsafe;
    }
    
    EXPORT isize string_find_first_char_vanilla(String string, char search_for, isize from)
    {
        ASSERT(from >= 0);
        for(isize i = from; i < string.size; i++)
            if(string.data[i] == search_for)
                return i;

        return -1;
    }

    EXPORT isize string_find_first_char_unsafe(String string, char search_for, isize from)
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
        u64 search_mask = broadcast64((u8) search_for); //here the cast to unsigned is important! Else we get 0xffffffff....{search_for} instead of 0x0000000...{search_for}!
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
    
    #include <intrin.h>
    EXPORT isize string_find_first_char_sse(String string, char c, isize from)
    {
        ASSERT(from >= 0);
        if(string.size == 0 || from >= string.size)
            return -1;

        char* end = (char*) string.data + string.size;
        char* start = (char*) string.data + from;
        char* aligned_data = (char*) align_backward(start, 16);

        isize overread_before = start - aligned_data;
        u32 overread_mask = ~(u32) 0 << (u32) overread_before; 
        u32 vmask = 0;
        
        __m128i* cursor = (__m128i*) aligned_data;   
        __m128i m0 = _mm_set1_epi8(c);         
            
        for (; cursor < (__m128i*) end; cursor ++)
        {      
            __m128i v0 = _mm_load_si128(cursor);     
            __m128i v1 = _mm_cmpeq_epi8(v0, m0);   
            vmask = (u32) _mm_movemask_epi8(v1);     
            vmask &= overread_mask;
            if (vmask != 0)                          
                goto found;    
                
            overread_mask = ~(u32) 0;
        }

        return -1;

        found:
        int first = platform_find_first_set_bit32(vmask);
        isize offset = first + (isize) cursor - (isize) string.data;
        if(offset >= string.size)
            return -1;
        else
            return offset;
    }

    EXPORT void memset_pattern(void *field, isize field_size, const void* pattern, isize pattern_size)
    {
        if (field_size <= pattern_size)
            memcpy(field, pattern, field_size);
        else if(pattern_size == 0)
            memset(field, 0, field_size);
        else
        {
            isize cursor = pattern_size;
            isize copy_size = pattern_size;

            // make one full copy
            memcpy((char*) field, pattern, pattern_size);
        
            // now copy from destination buffer, doubling size each iteration
            for (; cursor + copy_size < field_size; copy_size *= 2) 
            {
                memcpy((char*) field + cursor, field, copy_size);
                cursor += copy_size;
            }
        
            // copy any remainder
            memcpy((char*) field + cursor, field, field_size - cursor);
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
    
    EXPORT String string_duplicate(Arena* arena, String string)
    {
        char* data = (char*) arena_push_nonzero(arena, string.size + 1, 1);
        memcpy(data, string.data, string.size);
        data[string.size] = '\0';
        String out = {data, string.size};

        return out;
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

    EXPORT String_Builder builder_from_string(String string, Allocator* allocator)
    {
        String_Builder builder = {allocator};
        builder_assign(&builder, string);
        return builder;
    }

    EXPORT String_Builder builder_from_cstring(const char* cstring, Allocator* allocator)
    {
        return builder_from_string(string_make(cstring), allocator);
    }

    EXPORT bool builder_is_equal(String_Builder a, String_Builder b)
    {
        return string_is_equal(a.string, b.string);
    }
    
    EXPORT int builder_compare(String_Builder a, String_Builder b)
    {
        return string_compare(a.string, b.string);
    }
#endif