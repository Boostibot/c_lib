#ifndef MODULE_SORT
#define MODULE_SORT

// This file provides a replacement for the `qsort` stdlib.h function in a form of custom sorting functions. 
// By abusing __forceinline and compiler optimalizations we achieve similar effect to c++ templates. 
// The calls to comparison function are fully inlined and replaced with efficient assembly, memcpy calls 
// are replaced with mov instructions. We implement insertion sort, heap sort, quick sort and merge sort as well
// as few convenience functions. 
// The heapsort and quicksort routines are heavily optimized and reach state of the art performance. 
// On random integers hqsort is about 20% faster tan MSVC std::sort and on par with pdqsort. On large sizes (> 3000)
// we use our efficient heapsort implementation and consistently outperform pdqsort by about 20%-30% (as of 9/3/2024).
 
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifndef SORT_API
    #if defined(_MSC_VER)
        #define SORT_API static __forceinline
    #elif defined(__GNUC__) || defined(__clang__)
        #define SORT_API static __attribute__((always_inline)) inline
    #else
        #define SORT_API static inline
    #endif

    #define MODULE_IMPL_SORT
#endif

typedef bool (*Is_Less_Func)(const void* a, const void* b, void* context); 
typedef int64_t isize;

//Sorts items from smallest to biggest using the is_less comparison function. Similar to qsort. 
//Performs quicksort for medium sized arrays and optimized heap sort for large arrays
SORT_API void  hqsort(void* items, isize item_count, isize item_size, Is_Less_Func is_less, void* context);
    
//The following 4 functions sort the input items using the is_less comparison function. 
//They require a space for X item_size sized items to be provided by the user. Additionally the storage needs to be aligned according to the sorted type.
SORT_API void  insertion_sort(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context);
SORT_API void  heap_sort(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context);
SORT_API void  quick_sort(void* items, void* space_for_two_items, isize heap_sort_from, isize item_count, isize item_size, Is_Less_Func is_less, void* context);
    
// Sorts input just like the previous functions. Uses temp array which needs to have the same size as input to ping-pong data back and forth. 
// If dont_copy_back == true then leaves the final sorted result in input or temp, whereever it happened to be resided as a result of teh sorting algorithm. 
// Returns pointer to the buffer containing the sorted items (for dont_copy_back == false always returns input).
SORT_API void* merge_sort(void* __restrict input, void* __restrict temp, bool dont_copy_back, isize item_count, isize item_size, Is_Less_Func is_less, void* context);
//Merges sorted arrays a and b into output in O(n) time such that output is sorted.
SORT_API void  merge_sorted(void* __restrict output, const void* a, isize a_len, const void* b, isize b_len, isize item_size, Is_Less_Func is_less, void* context);

//Binary searches for an index I such that `search_for <= sorted_items[I]` where `I = lower_bound(search_for, sorted_items,...)`. 
//If no such index exists (search_for is bigger then everything in the sorted_items) then returns item_count.
SORT_API isize lower_bound(const void* search_for, const void* sorted_items, isize item_count, isize item_size, Is_Less_Func is_less, void* context);
//Same as lower_bound but if the search_for is bigger then everything in the sorted_items, the result is undefined.
SORT_API isize lower_bound_no_fail(const void* search_for, const void* sorted_items, isize item_count, isize item_size, Is_Less_Func is_less, void* context);

//================= Various settings ==================
#ifndef HEAP_SORT_FROM
    #define HEAP_SORT_FROM 2800
#endif

#ifndef INSERTION_SORT_TO
    #define INSERTION_SORT_TO 32
#endif

#ifndef HEAP_SORT_TWO_PHASE_BUBBLING_FROM
    #define HEAP_SORT_TWO_PHASE_BUBBLING_FROM 1300
#endif
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_SORT)) && !defined(MODULE_HAS_IMPL_SORT)
#define MODULE_HAS_IMPL_SORT

    #ifndef ASSERT
        #include <assert.h>
        #define ASSERT(x) assert(x)
        #define REQUIRE(x) assert(x)
    #endif

    //================= Convenience macros ==================
    #define AT(I)        ((char*) items + (I)*item_size)
    #define SWAP_STAT(a, b) do { \
        void* x = (void*) (a); \
        void* y = (void*) (b); \
        char s[sizeof *(a)]; (void) s; \
        memcpy(s, x, sizeof *(a)); \
        memcpy(x, y, sizeof *(a)); \
        memcpy(y, s, sizeof *(a)); \
    } while(0) \

    #define SWAP_DYN(a, b, temp) do { \
        void* x = (void*) (a); \
        void* y = (void*) (b); \
        void* s = (void*) (temp); \
        memcpy(s, x, item_size); \
        memcpy(x, y, item_size); \
        memcpy(y, s, item_size); \
    } while(0) \

    SORT_API void insertion_sort(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        REQUIRE(item_count >= 0 && item_size > 0 && (item_count == 0 || (items && space_for_one_item && is_less)));

        // This is a version of insertion sort inspired by the implementation one can 
        // find in pdqsort https://github.com/orlp/pdqsort/blob/b1ef26a55cdb60d236a5cb199c4234c704f46726/pdqsort.h#L77.
        // The basic idea is to avoid one store when the item is already in its place. 
        // This is a nice optimalization that makes it about 15%-40% faster on ints and even more on large data types.
        for (isize iter = 1; iter < item_count; iter++) {
            isize i = iter;
            isize j = iter - 1;
            if(is_less(AT(i), AT(j), context))
            {
                memcpy(space_for_one_item, AT(i), item_size);
                do {
                    memcpy(AT(i--), AT(j), item_size);
                } while(i > 0 && is_less(space_for_one_item, AT(--j), context));
            
                memcpy(AT(i), space_for_one_item, item_size);
            }
        }
    }

    SORT_API void heap_bubble_down_traditional(void* items, void* space_for_one_item, isize heap_top, isize heap_one_past_last, isize item_size, Is_Less_Func is_less, void* context);
    SORT_API void heap_bubble_down_two_phase(void* items, const void* value, isize heap_top, isize heap_one_past_last, isize item_size, Is_Less_Func is_less, void* context);

    SORT_API void heap_sort(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        REQUIRE(item_count >= 0 && item_size > 0 && (item_count == 0 || (items && space_for_one_item && is_less)));

        //Make heap (if we are not just calling insertion sort)
        if(item_count > INSERTION_SORT_TO) {
            for(isize node = item_count/2; node-- > 0;)
                heap_bubble_down_traditional(items, space_for_one_item, node, item_count, item_size, is_less, context);
        }
    
        // Swap first (biggest) and last item in the heap, declare the heap to be one value smaller
        // and restore heap property from the top in a proccess called bubble down or sift down. 
        // Repeat until no more items in the heap (or until less then some small amount and then insertion sort the rest).
        // 
        // There are multiple approaches to bubbling down. Probably the most common which can be found on Wikipedia
        // and GeeksForGeeks under "heap sort" article (as of 9/2/2024) uses swaps for all of its operations. Its simple
        // and really fast for small to moderate heaps (< 1300 items). 
        isize n = item_count;
        if(n < HEAP_SORT_TWO_PHASE_BUBBLING_FROM) 
        {
            for(; n > INSERTION_SORT_TO; n -= 2) {
                //Another quite cute trick is to not pop only the highest element, but also the bigger of its children.
                // Then we first restore the heap property to the popped child and then to the root. Even though this shouldnt
                // Have a drastic effect as its pretty much just 2 loop unrolling, it speed the sort on medium sizes by about 30%. 
                //
                // This can be also used for the other `heap_bubble_down_two_phase` in the branch below but I didnt find any perf
                // improvements there so I dropped it for simplicity.
                isize bigger = is_less(AT(1), AT(2), context) ? 2 : 1;
                SWAP_DYN(AT(0), AT(n-1), space_for_one_item);
                SWAP_DYN(AT(bigger), AT(n-2), space_for_one_item);
            
                heap_bubble_down_traditional(items, space_for_one_item, bigger, n-2, item_size, is_less, context);
                heap_bubble_down_traditional(items, space_for_one_item, 0,      n-2, item_size, is_less, context);
            }
        }
        // The second algorithm for bubbling down inspired by the MSVC STL implementation
        // (https://github.com/microsoft/STL/blob/91e425587a0b9b5ac0d1005844c661fee2e2813b/stl/inc/algorithm#L6704)
        // is based on the key insight that the last value which we are swapping to the place of the first item in the heap is
        // likely very very small. Thus the bubbling down is likely to visit most of the log2(n) levels in the heap. 
        // Because of this we want to make the bubbling down as cheap as possible. For this we drop the swap as a primitive
        // and instead start thinking about "holes". I will be referring to this last value as L. 
        //
        // L is stored in a local variable and the first value is copied to the last. This creates a "hole" located at the first 
        // item of the heap. Now we perform the bubbling down in two phases. First we move the hole all the way to the bottom,
        // making sure the heap property is preserved. We do this without looking at the value L by simply assuming its smaller then
        // all the other values. Once we reach the bottom we bubble upwards now considering at the value L. Because L is probably
        // small the upwards phase will be usually short. The we store L at the final value. This procedure saves us around
        // log2(n) comparisons with L and also cuts down the number of writes by 2x. This is because we are no longer swapping
        // elements, thus only write is required in place of two.
        else
        {
        
            for(; n > INSERTION_SORT_TO; n -= 1) {
                memcpy(space_for_one_item, AT(n-1), item_size);
                memcpy(AT(n-1), AT(0), item_size);
                heap_bubble_down_two_phase(items, space_for_one_item, 0, n-1, item_size, is_less, context);
            }
        }

        insertion_sort(items, space_for_one_item, n, item_size, is_less, context);
    }

    SORT_API void heap_bubble_down_traditional(void* items, void* space_for_one_item, isize heap_top, isize heap_one_past_last, isize item_size, Is_Less_Func is_less, void* context)
    {
        REQUIRE(heap_top < heap_one_past_last && items != NULL);
        while(true) 
        {
            isize max_i = heap_top;
            isize left = 2*heap_top + 1;
            isize right = 2*heap_top + 2;

            if (left < heap_one_past_last  && is_less(AT(max_i), AT(left), context)) 
                max_i = left;
            if (right < heap_one_past_last && is_less(AT(max_i), AT(right), context)) 
                max_i = right;

            if (max_i == heap_top)
                break;

            SWAP_DYN(AT(heap_top), AT(max_i), space_for_one_item);
            heap_top = max_i;
        }
    }

    SORT_API void heap_bubble_down_two_phase(void* items, const void* value, isize heap_top, isize heap_one_past_last, isize item_size, Is_Less_Func is_less, void* context) 
    {
        isize hole = heap_top;
        isize max_non_leaf = (heap_one_past_last - 1) / 2;

        //Bubbles the hole down selecting the larger child.
        //By using the larger child as the parent the heap property is kept.
        for (isize i = hole; i < max_non_leaf; ) { 
            i = 2*i + 2;
            i -= is_less(AT(i), AT(i - 1), context);
        
            memcpy(AT(hole), AT(i), item_size);
            hole = i;
        }

        //Bubble up
        //@NOTE: from this point onward can be a function heap_bubble_up which can be used to insert items from the back.
        //       This can be very useful when implementing priority queues 
        for (isize i = (hole - 1)/2; hole > heap_top; i = (hole - 1)/2) 
        {         
            if(is_less(AT(i), value, context) == false)
                break;

            memcpy(AT(hole), AT(i), item_size);
            hole = i;
        }

        memcpy(AT(hole), value, item_size);
    }


    SORT_API void quick_sort(void* items, void* space_for_two_items, isize heap_sort_from, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        //Pretty standard quicksort implementation. We dont use any fanciness (not even Tukey's ninther) as
        // I did not find it to impact the running time significantly in the usual case of random data.
        // 
        // Probably the most nonstandard thing here is the explicit stack handling below.
        // We cannot use "real" recursion because that stops the compiler
        // from being able to inline everything to the parent function. This is bad because it
        // also stops the passed in comparison function / memcpy calls from being inlined in,
        // thus essentially going back to qsort approach 
        isize depth = 0;
        isize los[64]; (void) los;
        isize his[64]; (void) his;
        isize unbalances[64]; (void) unbalances;

        //The region [lo, hi] which we are partitioning
        isize lo = 0;
        isize hi = item_count-1;

        void* pivot = space_for_two_items;
        void* swap_space = (char*) space_for_two_items + item_size;

        //We allow at maximum log2_n "highly unbalanced" (bad) partitions (see below how it is exactly calculated).
        //If we exceed that we switch to our highly optimized heapsort instead. This keeps this algorithm O(nlog(n)) 
        // no matter the input data
        isize log2_n = 0;
        {
            isize n_copy = item_count;
            while (n_copy >>= 1) ++log2_n;
        }
        isize unbalanced = log2_n;
        if(item_count >= heap_sort_from)
            goto big_heap_sort;

        //for depth >= 0
        for(;;)
        {
            recurse: 
            for(;;)
            {
                //if small amount of items use insertion sort and "return" from this recursion
                isize size = hi - lo + 1; 
                if(size <= INSERTION_SORT_TO)
                {
                    insertion_sort(AT(lo), swap_space, size, item_size, is_less, context);
                    break;
                }
        
                //median of tree as a pivot
                isize i = lo, j = (lo + hi)/2, k = hi;
                if (is_less(AT(k), AT(i), context)) SWAP_DYN(AT(k), AT(i), swap_space);
                if (is_less(AT(j), AT(i), context)) SWAP_DYN(AT(j), AT(i), swap_space);
                if (is_less(AT(k), AT(j), context)) SWAP_DYN(AT(k), AT(j), swap_space);

                //partition
                memcpy(pivot, AT(j), item_size);
                while (i <= k) {            
                    while (is_less(AT(i), pivot, context))
                        i++;
                    while (is_less(pivot, AT(k), context))
                        k--;
                    if (i <= k) {
                        SWAP_DYN(AT(i), AT(k), swap_space);
                        i++;
                        k--;
                    }
                }

                //Detect unbalanced partitions (see above for why). If too many
                // More then log2_n unbalanced partitions "return" from this recursion
                // and use heap sort instead.
                isize l_size = k - lo;
                isize r_size = hi - i;
                bool is_highly_unbalanced = (uint64_t)l_size < (uint64_t)size/8 
                                         || (uint64_t)r_size < (uint64_t)size/8;
                unbalanced -= is_highly_unbalanced;
                if(unbalanced <= 0)
                    break;
            
                //Recur to the side with fewer elements.
                //The other side is covered in the next iteration of the while loop.
                //This prevents us from using O(n) stack space in pathological cases.
                // (We always select smaller half which is smaller then `size`/2. 
                //  Iterate this relation and we arrive at O(log2_n) stack space at max).
                unbalances[depth] = unbalanced;
                if(l_size < r_size)
                {
                    los[depth] = i;
                    his[depth] = hi;
                
                    depth += 1;
                    hi = k;
                    goto recurse;
                }
                else
                {
                    los[depth] = lo;
                    his[depth] = k;

                    depth += 1;
                    lo = i;
                    goto recurse;
                }
            }
        
            //If we exited because of unbalanced, do heap sort
            if(unbalanced <= 0)
            {
                big_heap_sort:
                heap_sort(AT(lo), swap_space, hi - lo + 1, item_size, is_less, context);
            }

            //pop explicit stack 
            depth--;
            if(depth < 0)
                break;

            ASSERT(0 <= depth && depth < 64);
            lo = los[depth];
            hi = his[depth];
            unbalanced = unbalances[depth];
        }
    }

    #if defined(_MSC_VER)
        #include <intrin.h>
        #define _SORT_PREFETCH(ptr)                                 _mm_prefetch((const char*) (void*) (ptr), _MM_HINT_T0)
        #define _SORT_ALIGNED(bytes)                                __declspec(align(bytes))
    #elif defined(__GNUC__) || defined(__clang__)
        #define _SORT_PREFETCH(ptr)                                 __builtin_prefetch(ptr)
        #define _SORT_ALIGNED(bytes)                                __attribute__((aligned(bytes)))
    #else 
        #define _SORT_PREFETCH(ptr)
        #define _SORT_ALIGNED(bytes)
    #endif 

    SORT_API void  hqsort(void* items, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        _SORT_ALIGNED(64) uint64_t buffer[128]; (void) buffer;
        void* ptr = buffer;
        if(2*item_size > sizeof(buffer)) ptr = malloc(2*item_size);
        quick_sort(items, ptr, HEAP_SORT_FROM, item_count, item_size, is_less, context);
        if(2*item_size > sizeof(buffer)) free(ptr);
    }
    
    SORT_API isize lower_bound_no_fail(const void* search_for, const void* sorted_items, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        REQUIRE(item_count >= 0 && item_size > 0 && (item_count == 0 || (search_for && sorted_items && is_less)));
        const void* items = sorted_items; 
        isize count = item_count;
        while (count > 1) {
            isize half = (count + 1) / 2;
            count -= half;

            _SORT_PREFETCH(AT(count / 2 - 1));
            _SORT_PREFETCH(AT(half + count / 2 - 1));
        
            bool was_less = is_less(AT(half - 1), search_for, context);
            items = AT(was_less * half);
        }
        
        isize lower_bound_i = ((char*) items - (char*) sorted_items)/item_size;
        ASSERT(0 <= lower_bound_i && lower_bound_i <= item_count);
        return lower_bound_i;
    }

    SORT_API isize lower_bound(const void* search_for, const void* sorted_items, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        if(item_count == 0)
            return 0;

        const void* items = sorted_items; 
        isize at = lower_bound_no_fail(search_for, items, item_count, item_size, is_less, context);
        items = AT(at + (int) is_less(AT(at), search_for, context));

        return ((char*) items - (char*) sorted_items)/item_size;
    }

    SORT_API void merge_sorted(void* __restrict output, const void* a, isize a_len, const void* b, isize b_len, isize item_size, Is_Less_Func is_less, void* context)
    {
        #define AT_OF(items, I) ((char*) (items) + (I)*item_size)
        isize ai = 0;
        isize bi = 0;
        while(ai < a_len && bi < b_len)
        {
            if(is_less(AT_OF(a, ai), AT_OF(b, bi), context))
            {
                memcpy(AT_OF(output, ai + bi), AT_OF(a, ai), item_size);
                ai++;
            }
            else
            {
                memcpy(AT_OF(output, ai + bi), AT_OF(b, bi), item_size);
                bi++;
            }
        }

        if(ai < a_len)
            memcpy(AT_OF(output, ai + bi), AT_OF(a, ai), (a_len - ai)*item_size);
        else
            memcpy(AT_OF(output, ai + bi), AT_OF(b, bi), (b_len - bi)*item_size);

    }

    SORT_API void* merge_sort(void* __restrict input, void* __restrict temp, bool dont_copy_back, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
    {
        void* __restrict a = input;
        void* __restrict b = temp;
        #define SORT_MIN(a, b) (a) < (b) ? (a) : (b)

        isize N = item_count;
        for (isize i = 0; i < N; i += INSERTION_SORT_TO)
            insertion_sort(AT_OF(a, i), temp, SORT_MIN(INSERTION_SORT_TO, N - i), item_size, is_less, context);

        for (isize width = INSERTION_SORT_TO; width < N; width = 2 * width)
        {
            for (isize i = 0; i < N; i += 2*width)
                merge_sorted(AT_OF(b, i), 
                    AT_OF(a, i), SORT_MIN(width, N - i), 
                    AT_OF(a, i + width), SORT_MIN(width, N - i - width),
                    item_size, is_less, context);
          
            SWAP_STAT(&a, &b);
        }
    
        if(dont_copy_back == false && a != input)
        {
            memcpy(input, a, N*item_size);
            a = input;
        }

        return a;
        #undef AT_OF
        #undef SORT_MIN
    }

#undef AT
#undef SWAP_STAT
#undef SWAP_DYN

#endif

//================== TESTS =======================
#if defined(MODULE_TEST_SORT) || defined(MODULE_ALL_TEST) 
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

    int _sort_rand_exponential_distribution(int max_log2, float jitter_ammount)
    {
        int rand_log2 = rand() % max_log2;
        float rand_jitter = (float) rand() / RAND_MAX;
        rand_jitter = rand_jitter*2 - 1; //in range [-1, 1]

        int final_val = (int) ((float) (1 << rand_log2) * (1 + rand_jitter*jitter_ammount));
        if(final_val > (1 << max_log2))
            final_val = (1 << max_log2);
        if(final_val < 0)
            final_val = 0;
        return final_val;
    }

    #include <time.h>
    #include <stdio.h>
    static void test_sort(double seconds)
    {
        #ifndef TEST
            #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
        #endif
        
        enum {
            MAX_SIZE_LOG2 = 16, 
            MAX_SIZE = 1 << MAX_SIZE_LOG2
        };
    
        const char* words[] = {
              "Lorem","ipsum","dolor","sit","amet,","consectetur","adipiscing","elit,","sed","do","eiusmod","tempor","incididunt","ut",
              "labore","et","dolore","magna","aliqua.","Ut","enim","ad","minim","veniam,","quis","nostrud","exercitation","ullamco",
              "laboris","nisi","ut","aliquip","ex","ea","commodo","consequat.","Duis","aute","irure","dolor","in","reprehenderit",
              "in","voluptate","velit","esse","cillum","dolore","eu","fugiat","nulla","pariatur.","Excepteur","sint","occaecat","cupidatat",
              "non","proident,","sunt","in","culpa","qui","officia","deserunt","mollit","anim","id","est","laborum"
        };

        void* items_randomized = malloc(MAX_SIZE);
        void* items_refernce_sorted = malloc(MAX_SIZE);
        void* items_sorted = malloc(MAX_SIZE);
        void* items_temp = malloc(MAX_SIZE);

        srand(clock());
        for(double start = (double) clock() / (double) CLOCKS_PER_SEC; 
            (double) clock() / (double) CLOCKS_PER_SEC < start + seconds;)
        {
            //i32
            {
                int size = _sort_rand_exponential_distribution(MAX_SIZE_LOG2, 0.5)/(int)sizeof(int32_t);
                int32_t* items_val = (int32_t*) items_randomized;
                for(int i = 0; i < size; i++)
                    items_val[i] = rand();

                size_t bytes = (size_t) size * sizeof(int32_t);
                memcpy(items_refernce_sorted, items_randomized, bytes);
                qsort(items_refernce_sorted, size, sizeof(int32_t), _sort_test_i32_comp);
                
                memcpy(items_sorted, items_randomized, bytes);
                insertion_sort(items_sorted, items_temp, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                merge_sort(items_sorted, items_temp, false, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                quick_sort(items_sorted, items_temp, HEAP_SORT_FROM, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                heap_sort(items_sorted, items_temp, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                hqsort(items_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);

                //lower bound tests
                if(size > 0)
                {   
                    int32_t* items_sorted_val = (int32_t*) items_sorted;
                    int32_t min_val = items_sorted_val[0];
                    int32_t max_val = items_sorted_val[size - 1];

                    //concrete value should be found exactly
                    isize found_max = lower_bound(&max_val, items_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                    TEST(max_val == ((int32_t*) items_sorted)[found_max]);
                    isize found_min = lower_bound(&min_val, items_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                    TEST(min_val == ((int32_t*) items_sorted)[found_min]);

                    //some interpolated value should work according to lower bound spec
                    int32_t interp_legal_val = (min_val + max_val)/2;
                    isize lower = lower_bound(&interp_legal_val, items_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                    TEST(0 <= lower && lower < size);
                    TEST(interp_legal_val <= ((int32_t*) items_sorted)[lower]);
                
                    //Invalid value should not be found
                    int32_t bigger = max_val + 1;
                    isize should_be_size = lower_bound(&bigger, items_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                    TEST(should_be_size == size);
                }
                else
                {
                    int32_t any = rand();
                    isize should_be_size = lower_bound(&any, items_sorted, size, sizeof(int32_t), _sort_test_i32_less, NULL);
                    TEST(should_be_size == size);
                }
            }

            //cstring
            {
                int size = _sort_rand_exponential_distribution(MAX_SIZE_LOG2, 0.5)/(int)sizeof(const char*);
                const char** items_val = (const char**) items_randomized;
                for(int i = 0; i < size; i++)
                    items_val[i] = words[(size_t) rand() % (sizeof(words)/sizeof(*words))];

                size_t bytes = (size_t) size * sizeof(const char*);
                memcpy(items_refernce_sorted, items_randomized, bytes);
                qsort(items_refernce_sorted, (size_t) size, sizeof(const char*), _sort_test_cstring_comp);
                
                memcpy(items_sorted, items_randomized, bytes);
                insertion_sort(items_sorted, items_temp, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                merge_sort(items_sorted, items_temp, false, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                quick_sort(items_sorted, items_temp, HEAP_SORT_FROM, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                heap_sort(items_sorted, items_temp, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
                
                memcpy(items_sorted, items_randomized, bytes);
                hqsort(items_sorted, size, sizeof(const char*), _sort_test_cstring_less, NULL);
                TEST(memcmp(items_refernce_sorted, items_sorted, bytes) == 0);
            }
        }
    
        free(items_randomized);
        free(items_refernce_sorted);
        free(items_sorted);
        free(items_temp);
    }
#endif