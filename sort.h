#ifndef JOT_SORT
#define JOT_SORT

//This file inline implementation of quicksort and insertion sort. By abusing __forceinline and compiler optimizalizations we
// achieve similar effect to c++ templates. The calls to comparison function are fully inlined and replaced with efficient assembly, 
// memcpy calls are replaced with mov instructions. For ints the result runs only about 20% slower to fully concrete int implementation.
// For more complex data types (for example strings) the slowdown is even less percievable.
//Still this is about 30% then C++ MSVC std::sort and 7x faster then MSVC qsort (as of 8/30/2024).

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifndef ATTRIBUTE_INLINE_ALWAYS
    #if defined(_MSC_VER)
        #define ATTRIBUTE_INLINE_ALWAYS     __forceinline
        #define ATTRIBUTE_INLINE_NEVER      __declspec(noinline)
    #elif defined(__GNUC__) || defined(__clang__)
        #define ATTRIBUTE_INLINE_ALWAYS     __attribute__((always_inline)) inline
        #define ATTRIBUTE_INLINE_NEVER      __attribute__((noinline))
    #else
        #define ATTRIBUTE_INLINE_ALWAYS         
        #define ATTRIBUTE_INLINE_NEVER
    #endif
#endif

static ATTRIBUTE_INLINE_ALWAYS 
void quicksort(void* items, int64_t item_count, int64_t item_size, bool (*is_less)(const void* a, const void* b, void* context), void* context);

static ATTRIBUTE_INLINE_ALWAYS 
void insertion_sort(void* items, int64_t item_count, int64_t item_size, bool (*is_less)(const void* a, const void* b, void* context), void* context);

static ATTRIBUTE_INLINE_ALWAYS 
void insertion_sort_inline(void* items, int64_t item_count, int64_t item_size, void* space_for_one_item, bool (*is_less)(const void* a, const void* b, void* context), void* context);

static ATTRIBUTE_INLINE_ALWAYS 
void quicksort_inline(void* items, int64_t item_count, int64_t item_size, void* space_for_two_items, bool (*is_less)(const void* a, const void* b, void* context), void* context);

//======================= Inline implementation below =======================
static ATTRIBUTE_INLINE_ALWAYS 
void insertion_sort_inline(void* items, int64_t item_count, int64_t item_size, void* space_for_one_item, bool (*is_less)(const void* a, const void* b, void* context), void* context)
{
    #define AT(I) ((char*) items + (I)*item_size)
    for (int64_t i = 1; i < item_count; ++i) {
        memcpy(space_for_one_item, AT(i), item_size);
        int64_t j = i - 1;

        while (j >= 0 && is_less(space_for_one_item, AT(j), context)) {
            memcpy(AT(j + 1), AT(j), item_size);
            j --;
        }
        memcpy(AT(j + 1), space_for_one_item, item_size);
    }
    #undef AT
}

static ATTRIBUTE_INLINE_ALWAYS 
void quicksort_inline(void* items, int64_t item_count, int64_t item_size, void* space_for_two_items, bool (*is_less)(const void* a, const void* b, void* context), void* context)
{
    #define AT(I) ((char*) items + (I)*item_size)
    #define SWAP_(a, b) do { \
        void* x = (a); void* y = (b); \
        memcpy(swap_space, x, item_size); \
        memcpy(x, y, item_size); \
        memcpy(y, swap_space, item_size); \
    } while(0) \
    
    void* pivot = space_for_two_items;
    void* swap_space = (char*) space_for_two_items + item_size;

    int64_t los[64]; 
    int64_t his[64];
    los[0] = 0;
    his[0] = item_count - 1;

    for(int depth = 0; depth >= 0; depth--) {
        recurse: {
            assert(depth < 64 && "can happen only if comparison function is broken");
            int64_t lo = los[depth];
            int64_t hi = his[depth];

            while (lo < hi) {
                int64_t size = hi - lo + 1; 
                if(size <= 32)
                {
                    insertion_sort_inline(AT(lo), size, item_size, swap_space, is_less, context);
                    break;
                }
        
                int64_t i = lo, j = (lo + hi)/2, k = hi;
                if (is_less(AT(k), AT(i), context)) SWAP_(AT(k), AT(i));
                if (is_less(AT(j), AT(i), context)) SWAP_(AT(j), AT(i));
                if (is_less(AT(k), AT(j), context)) SWAP_(AT(k), AT(j));
                memcpy(pivot, AT(j), item_size);
        
                while (i <= k) {            
                    while (is_less(AT(i), pivot, context))
                        i++;
                    while (is_less(pivot, AT(k), context))
                        k--;
                    if (i <= k) {
                        SWAP_(AT(i), AT(k));
                        i++;
                        k--;
                    }
                }

                //recur to the side with fewer elements 
                //the other side is covered in the next iteration of the while loop
                if(k - lo < hi - i)
                {
                    los[depth] = i;
                
                    depth += 1;
                    los[depth] = lo;
                    his[depth] = k;
                    goto recurse;
                }
                else
                {
                    his[depth] = k;

                    depth += 1;
                    los[depth] = i;
                    his[depth] = hi;
                    goto recurse;
                }
            }
        }
    }

    #undef AT
    #undef SWAP_
}

static ATTRIBUTE_INLINE_ALWAYS 
void quicksort(void* items, int64_t item_count, int64_t item_size, bool (*is_less)(const void* a, const void* b, void* context), void* context)
{
    int64_t buffer[256];
    void* ptr = buffer;
    if(2*item_size > sizeof(buffer)) ptr = malloc(2*item_size);
    quicksort_inline(items, item_count, item_size, ptr, is_less, context);
    if(2*item_size > sizeof(buffer)) free(ptr);
}

static ATTRIBUTE_INLINE_ALWAYS 
void insertion_sort(void* items, int64_t item_count, int64_t item_size, bool (*is_less)(const void* a, const void* b, void* context), void* context)
{
    int64_t buffer[256];
    void* ptr = buffer;
    if(item_size > sizeof(buffer)) ptr = malloc(item_size);
    insertion_sort_inline(items, item_count, item_size, ptr, is_less, context);
    if(item_size > sizeof(buffer)) free(ptr);
}

#endif

//================== TESTS =======================
#if defined(JOT_TEST_SORT) || defined(JOT_ALL_TEST) 
    static bool _sort_test_i32_less(const void* a, const void* b, void* context)
    {
        (void) context;
        return *(int32_t*) a < *(int32_t*) b;
    }

    static int _sort_test_i32_comp(const void* a, const void* b)
    {
        int32_t av = *(int32_t*) a; 
        int32_t bv = *(int32_t*) b; 
        return (av > bv) - (av < bv);
    }

    static bool _sort_test_cstring_less(const void* a, const void* b, void* context)
    {
        (void) context;
        const char* av = *(const char**) a; 
        const char* bv = *(const char**) b; 
        return strcmp(av, bv) < 0;
    }

    static int _sort_test_cstring_comp(const void* a, const void* b)
    {
        const char* av = *(const char**) a; 
        const char* bv = *(const char**) b; 
        return strcmp(av, bv);
    }

    #include <time.h>
    #include <stdio.h>
    static void test_sort(double seconds)
    {
        #ifndef TEST
            #define TEST(x) (!(x) ? printf("test_sort TEST('%s') failed\n", #x), abort() : (void) 0)
        #endif

        enum {MAX_SIZE = 1 << 16, ZERO_SIZE_CHANCE = 10};
    
        void* items_randomized = malloc(MAX_SIZE);
        void* items_insert_sorted = malloc(MAX_SIZE);
        void* items_quick_sorted = malloc(MAX_SIZE);
        void* items_q_sorted = malloc(MAX_SIZE);

        const char* words[] = {
            "", "a", "b", "c", "aa", "bb", "cc", "zzzzz"
            "hello", "hi", "goodbye"
        };
        enum {WORDS_COUNT = sizeof(words)/sizeof(*words)};

        srand(clock());
        for(double start = (double) clock() / (double) CLOCKS_PER_SEC; 
            (double) clock() / (double) CLOCKS_PER_SEC < start + seconds;)
        {
            //i32
            {
                int size = rand() % (MAX_SIZE / sizeof(int32_t));
                if(rand() % ZERO_SIZE_CHANCE == 0)
                    size = 0;

                int32_t* items_val = (int32_t*) items_randomized;
                for(int i = 0; i < size; i++)
                    items_val[i] = rand();

                size_t bytes = (size_t) size * sizeof(int32_t);
                memcpy(items_insert_sorted, items_randomized, bytes);
                memcpy(items_quick_sorted, items_randomized, bytes);
                memcpy(items_q_sorted, items_randomized, bytes);

                insertion_sort(items_insert_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                quicksort(items_quick_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                qsort(items_q_sorted, size, sizeof(int32_t), _sort_test_i32_comp);

                TEST(memcmp(items_q_sorted, items_insert_sorted, bytes) == 0);
                TEST(memcmp(items_q_sorted, items_quick_sorted, bytes) == 0);
            }

            //cstring
            {
                int size = rand() % (MAX_SIZE / sizeof(const char*));;
                if(rand() % ZERO_SIZE_CHANCE == 0)
                    size = 0;
                    
                const char** items_val = (const char**) items_randomized;
                for(int i = 0; i < size; i++)
                    items_val[i] = words[rand() % WORDS_COUNT];

                size_t bytes = (size_t) size * sizeof(const char*);
                memcpy(items_insert_sorted, items_randomized, bytes);
                memcpy(items_quick_sorted, items_randomized, bytes);
                memcpy(items_q_sorted, items_randomized, bytes);

                insertion_sort(items_insert_sorted, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                quicksort(items_quick_sorted, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                qsort(items_q_sorted, size, sizeof(const char*), _sort_test_cstring_comp);
                
                TEST(memcmp(items_q_sorted, items_insert_sorted, bytes) == 0);
                TEST(memcmp(items_q_sorted, items_quick_sorted, bytes) == 0);
            }
        }
    
        free(items_randomized);
        free(items_insert_sorted);
        free(items_quick_sorted);
        free(items_q_sorted);
    }
#endif
