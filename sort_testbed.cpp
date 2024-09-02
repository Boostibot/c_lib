#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <algorithm>

#define JOT_ALL_IMPL
//#define EXTERNAL static

#include "perf.h"
#include "profile.h"

void gen_numbers(int* nums, int count, int seed)
{
    srand(seed);
    for(int i = 0; i < count; i++)
        nums[i] = rand();
}

#include <algorithm>
#include <string.h>

#define SWAP_N(a, b, N) do { \
  char temp[N]; \
  memcpy(temp, a, N); \
  memcpy(a, b, N); \
  memcpy(b, temp, N); \
} while(0) \

#define SWAP(a, b) SWAP_N((a), (b), sizeof *(a))


#define AT(I)         ((char*) items + (I)*item_size)
#define SWAP_DYN(a, b) do { \
    void* x = a; void* y = b; \
    memcpy(swap_space, x, item_size); \
    memcpy(x, y, item_size); \
    memcpy(y, swap_space, item_size); \
} while(0) \

#ifndef SORT_HEAP_SORT_FROM
    #define SORT_HEAP_SORT_FROM 2800
#endif

#ifndef INSERTION_SORT_TO
    #define INSERTION_SORT_TO 32
#endif

#ifndef NO_SWAP_HEAP_SORT_FROM
    #define NO_SWAP_HEAP_SORT_FROM 1300
#endif
 
int int_compc_fast(const void* a, const void* b)
{
    int av = *(int*) a; 
    int bv = *(int*) b; 
    return (av > bv) - (av < bv);
}

bool int_less(const void* a, const void* b, void* context)
{
    int av = *(int*) a; 
    int bv = *(int*) b; 
    return (av < bv);
}

void insertion_sort(int arr[], int n)
{
    for (int i = 1; i < n; ++i) {
        int key = arr[i];
        int j = i - 1;

        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j --;
        }
        arr[j + 1] = key;
    }
}

#define SORT_API static ATTRIBUTE_INLINE_ALWAYS

typedef bool (*Is_Less_Func)(const void* a, const void* b, void* context); 

SORT_API void pdq_insertion_sort(int arr[], int N)
{
    for (int iter = 1; iter < N; iter++) {
        int i = iter;
        int j = iter - 1;
        if(arr[i] < arr[j])
        {
            int temp = arr[i];
            do {
                arr[i--] = arr[j];
            } while(i > 0 && temp < arr[--j]);
            
            arr[i] = temp;
        }
    }
}

SORT_API void gen_insertion_sort(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
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

#ifdef _MSC_VER
    #define PREFETCH(ptr) _mm_prefetch((const char*) (void*) (ptr), _MM_HINT_T0)
#else 
    #define PREFETCH(ptr) __builtin_prefetch(ptr)
#endif // _MSC_VER

int lower_bound(int x, int arr[], int n) 
{
    int *base = arr; 
    int len = n;
    while (len > 1) {
        int half = len / 2;
        len -= half;
        PREFETCH(&base[len / 2 - 1]);
        PREFETCH(&base[half + len / 2 - 1]);
        base += (base[half - 1] < x) * half;
    }
    return (int) (base - arr);
}


SORT_API isize gen_lower_bound(const void* search_for, const void* sorted_items, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    const void* items = sorted_items; 
    isize len = item_count;
    while (len > 1) {
        isize half = len / 2;
        len -= half;

        //prefetch next 
        PREFETCH(AT(len / 2 - 1));
        PREFETCH(AT(half + len / 2 - 1));
        
        bool was_less = is_less(AT(half - 1), search_for, context);
        items = AT(was_less * half);
    }

    isize lower_bound_i = ((char*) items - (char*) sorted_items)/item_size;
    return lower_bound_i;
}

bool is_sorted(int arr[], int n)
{
    for(int i = 1; i < n; i++)
        if(arr[i - 1] > arr[i])
            return false;
    return true;
}   

static ATTRIBUTE_INLINE_ALWAYS
bool gen_is_sorted(const void* items, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    for(isize i = 1; i < item_count; i++)
        if(is_less(AT(i), AT(i - 1), context))
            return false;
    return true;
}

static ATTRIBUTE_INLINE_ALWAYS
void heap_push_first(int arr[], int N, int root)
{
    while(true)
    {
        int max_i = root;
        int left = 2*root + 1;
        int right = 2*root + 2;

        if (left < N  && arr[left]  > arr[max_i]) max_i = left;
        if (right < N && arr[right] > arr[max_i]) max_i = right;

        if (max_i == root)
            break;

        SWAP(&arr[root], &arr[max_i]);
        root = max_i;
    }
}


ATTRIBUTE_INLINE_NEVER
void heap_sort(int arr[], int N)
{
    for(int node = N/2 + 1; node-- > 0;)
        heap_push_first(arr, N, node);

    for(int i = N; i-- > 0;) {
        SWAP(&arr[0], &arr[i]);
        heap_push_first(arr, i, 0);
    }
}

void heap_bubble_up(int* arr, int hole, int val, int top) 
{
    for (int i = (hole - 1)/2; hole > top; i = (hole - 1)/2) 
    {         
        if(arr[i] >= val)
            break;

        arr[hole] = arr[i];
        hole      = i;
    }

    arr[hole] = val;
}

void heap_bubble_down(int* arr, int hole, int val, int N) 
{
    int top = hole;
    int i   = hole;
    // Check whether i can have a child before calculating that child's index, since
    // calculating the child's index can trigger integer overflows
    int max_non_leaf = (N - 1) / 2;

    // move hole down to larger child
    while (i < max_non_leaf) { 
        i = 2*i + 2;
        i -= (arr[i] < arr[i - 1]);
        
        arr[hole] = arr[i];
        hole      = i;
    }

    //This was in MSVC implementation. I dont see why its necessary at all.
    //Removing it changed nothing. All heap properties are preserved even without it
    if(0)
    {
        // only child at bottom, move hole down to it
        if (i == max_non_leaf && N % 2 == 0) { 
            arr[hole] = arr[N - 1];
            hole      = N - 1;
        }
    }

    heap_bubble_up(arr, hole, val, top);
}

SORT_API void gen_heap_push_first(void* items, void* space_for_one_item, isize heap_top, isize heap_one_past_last, isize item_size, Is_Less_Func is_less, void* context)
{
    ASSERT(heap_top < heap_one_past_last && items != NULL);
    void* swap_space = space_for_one_item;
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

        SWAP_DYN(AT(heap_top), AT(max_i));
        heap_top = max_i;
    }
}

SORT_API void gen_heap_bubble_up(void* items, const void* value, isize heap_top, isize item_count, isize item_size, Is_Less_Func is_less, void* context) 
{
    isize hole = item_count;
    for (isize i = (hole - 1)/2; hole > heap_top; i = (hole - 1)/2) 
    {         
        if(is_less(AT(i), value, context) == false)
            break;

        memcpy(AT(hole), AT(i), item_size);
        hole = i;
    }

    memcpy(AT(hole), value, item_size);
}

SORT_API void gen_heap_bubble_down(void* items, const  void* value, isize heap_top, isize item_count, isize item_size, Is_Less_Func is_less, void* context) 
{
    isize hole = heap_top;
    isize i   = hole;
    isize max_non_leaf = (item_count - 1) / 2;

    //Bubbles the hole down selecting the larger child.
    //By using the larger child as the parent the heap property is kept.
    while (i < max_non_leaf) { 
        i = 2*i + 2;
        i -= is_less(AT(i), AT(i - 1), context);
        
        memcpy(AT(hole), AT(i), item_size);
        hole = i;
    }
    
    //This was in MSVC implementation. I dont see why its necessary at all.
    //Removing it changed nothing. All heap properties are preserved even without it
    if(0)
    {
        // only child at bottom, move hole down to it
        if (i == max_non_leaf && item_count % 2 == 0) { 
            memcpy(AT(hole), AT(item_count - 1), item_size);
            hole = item_count - 1;
        }
    }

    gen_heap_bubble_up(items, value, heap_top, hole, item_size, is_less, context);
}

void heap_push_last(int* arr, int heap_top, int heap_one_past_last)
{
    ASSERT(heap_top < heap_one_past_last && arr != NULL);
    int val = arr[heap_one_past_last - 1];
    heap_bubble_up(arr, heap_one_past_last-1, val, heap_top);
}


void heap_make(int* arr, int heap_top, int heap_one_past_last)
{
    ASSERT(heap_top <= heap_one_past_last && (arr != NULL || heap_top == heap_one_past_last));
    for(int node = heap_one_past_last/2; node-- > heap_top;)
        heap_push_first(arr, heap_one_past_last, node);
}

void heap_pop_max(int* arr, int heap_one_past_last)
{
    ASSERT(0 < heap_one_past_last && arr != NULL);
    int temp = arr[heap_one_past_last-1];
    arr[heap_one_past_last-1] = arr[0];
    heap_bubble_down(arr, 0, temp, heap_one_past_last-1);
}


bool heap_is_heap(int* arr, int heap_top, int heap_one_past_last)
{
    ASSERT(heap_top <= heap_one_past_last && (arr != NULL || heap_top == heap_one_past_last));
    int check_till = (heap_one_past_last-2)/2;
    for(int i = heap_top; i <= check_till; i++)
    {
        int left = i*2 + 1; 
        int right = i*2 + 2; 
        if(left < heap_one_past_last && arr[i] < arr[left]) 
            return false;
        if(right < heap_one_past_last && arr[i] < arr[right]) 
            return false;
    }

    return true;
}

SORT_API void gen_heap_push_last(void* items, void* space_for_one_item, isize heap_top, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    ASSERT(heap_top < item_count && items != NULL);
    memcpy(space_for_one_item, AT(item_count - 1), item_size);
    gen_heap_bubble_up(items, space_for_one_item, heap_top, item_count-1, item_size, is_less, context);
}

SORT_API void gen_heap_make(void* items, void* space_for_one_item, isize heap_top, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    ASSERT(heap_top <= item_count && (items != NULL || heap_top == item_count));
    for(isize node = item_count/2; node-- > heap_top;)
        gen_heap_push_first(items, space_for_one_item, node, item_count, item_size, is_less, context);
}

SORT_API void gen_heap_pop_max(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    ASSERT(0 < item_count && items != NULL);
    memcpy(space_for_one_item, AT(item_count-1), item_size);
    memcpy(AT(item_count-1), AT(0), item_size);
    gen_heap_bubble_down(items, space_for_one_item, 0, item_size, item_count, is_less,  context);
}

SORT_API bool gen_heap_is_heap(const void* items, void* space_for_one_item, isize heap_top, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    ASSERT(heap_top <= item_count && (items != NULL || heap_top == item_count));
    isize check_till = (item_count-2)/2;
    for(isize i = heap_top; i <= check_till; i++)
    {
        isize left = i*2 + 1; 
        isize right = i*2 + 2; 
        if(left < item_count && is_less(AT(i), AT(left), context)) 
            return false;
        if(right < item_count && is_less(AT(i), AT(right), context)) 
            return false;
    }

    return true;
}

enum {STD_LIKE_FROM = 1300};
enum {ISORT_FROM = 32};
void heap_sort_heap(int* heap, int heap_one_past_last)
{
    ASSERT(0 <= heap_one_past_last && (heap != NULL || heap_one_past_last == 0));
    int n = heap_one_past_last;
    if(n > STD_LIKE_FROM) 
    {
        for(; n > ISORT_FROM; n -= 1) {
            int temp = heap[n-1];
            heap[n-1] = heap[0];
            heap_bubble_down(heap, 0, temp, n-1);
        }
    }
    else
    {
        for(; n > ISORT_FROM; n -= 2) {
            int bigger = heap[1] > heap[2] ? 1 : 2;
            SWAP(&heap[0], &heap[n-1]);
            SWAP(&heap[bigger], &heap[n-2]);

            heap_push_first(heap, n-2, bigger);
            heap_push_first(heap, n-2, 0);
        }
    }

    pdq_insertion_sort(heap, n);
}

SORT_API void gen_heap_sort_heap(void* items, void* space_for_one_item, isize heap_one_past_last, isize item_size, Is_Less_Func is_less, void* context)
{
    void* swap_space = space_for_one_item;
    ASSERT(0 <= heap_one_past_last && (items != NULL || heap_one_past_last == 0));
    isize n = heap_one_past_last;
    if(n > NO_SWAP_HEAP_SORT_FROM) 
    {
        for(; n > INSERTION_SORT_TO; n -= 1) {
            memcpy(space_for_one_item, AT(n-1), item_size);
            memcpy(AT(n-1), AT(0), item_size);
            gen_heap_bubble_down(items, space_for_one_item, 0, n-1, item_size, is_less, context);
        }
    }
    else
    {
        for(; n > INSERTION_SORT_TO; n -= 2) {
            isize bigger = is_less(AT(1), AT(2), context) ? 2 : 1;
            SWAP_DYN(AT(0), AT(n-1));
            SWAP_DYN(AT(bigger), AT(n-2));
            
            gen_heap_push_first(items, space_for_one_item, bigger, n-2, item_size, is_less, context);
            gen_heap_push_first(items, space_for_one_item, bigger, 0, item_size, is_less, context);
        }
    }

    gen_insertion_sort(items, space_for_one_item, n, item_size, is_less, context);
}

SORT_API void gen_heap_sort(void* items, void* space_for_one_item, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    if(item_count > INSERTION_SORT_TO)
        gen_heap_make(items, space_for_one_item, 0, item_count, item_size, is_less, context);
    
    gen_heap_sort_heap(items, space_for_one_item, item_count, item_size, is_less, context);
}

ATTRIBUTE_INLINE_NEVER
void heap_sort_balanced(int arr[], int N)
{
    if(N > ISORT_FROM)
        heap_make(arr, 0, N);
    
    heap_sort_heap(arr, N);
}

ATTRIBUTE_INLINE_NEVER
void heap_sort_std_like(int arr[], int N)
{
    if(N > 32)
    {
        //make std heap
        for(int node = N/2; node-- > 0;)
            heap_push_first(arr, N, node);
        //for(int hole = N/2 + 1; hole-- > 0;) {
        //    int temp = arr[hole];
        //    heap_bubble_down(arr, hole, temp, N);
        //}
    }
    
    int n = N;
    for(; n > 32; n -= 1) {
        int temp = arr[n-1];
        arr[n-1] = arr[0];
        heap_bubble_down(arr, 0, temp, n-1);
    }
    
    pdq_insertion_sort(arr, n);
}

ATTRIBUTE_INLINE_NEVER
void heap_sort_std_like_two_swap(int arr[], int N)
{
    if(N > 32)
    {
        //make std heap
        for(int node = N/2; node-- > 0;)
            heap_push_first(arr, N, node);
    }

    int n = N;
    for(; n > 32; n -= 2) {
        
        int bigger_i = arr[1] > arr[2] ? 1 : 2;
        int bigger_temp = arr[n-2];
        arr[n-2] = arr[bigger_i];

        int biggest_temp = arr[n-1];
        arr[n-1] = arr[0];
        
        heap_bubble_down(arr, bigger_i, bigger_temp, n-2);
        heap_bubble_down(arr, 0, biggest_temp, n-2);
    }
    
    pdq_insertion_sort(arr, n);
}

ATTRIBUTE_INLINE_NEVER
void heap_sort_two_swap(int arr[], int N)
{
    if(N > 32)
    {
        for(int node = N/2; node-- > 0;)
            heap_push_first(arr, N, node);
    }

    int n = N;
    for(; n > 32; n -= 2) {
        int bigger = arr[1] > arr[2] ? 1 : 2;
        SWAP(&arr[0], &arr[n-1]);
        SWAP(&arr[bigger], &arr[n-2]);
            
        heap_push_first(arr, n-2, bigger);
        heap_push_first(arr, n-2, 0);
    }

    insertion_sort(arr, n);
}

static ATTRIBUTE_INLINE_ALWAYS
void merge_sorted(int* __restrict output, const int* __restrict a, int a_len, const int* __restrict b, int b_len)
{
    int ai = 0;
    int bi = 0;
    while(ai < a_len && bi < b_len)
    {
        if(a[ai] < b[bi])
            output[ai + bi] = a[ai++];
        else
            output[ai + bi] = b[bi++];
    }

    if(ai < a_len)
        memcpy(output + ai + bi, a + ai, (a_len - ai)*sizeof(int));
    else
        memcpy(output + ai + bi, b + bi, (b_len - bi)*sizeof(int));
}

static ATTRIBUTE_INLINE_ALWAYS
void* merge_sort_no_copy_back(int* __restrict input, int* __restrict temp, int N)
{
    int* __restrict a = input;
    int* __restrict b = temp;
    enum {ISORT = 32};

    for (int i = 0; i < N; i += ISORT)
        pdq_insertion_sort(a + i, MIN(ISORT, N - i));

    for (int width = ISORT; width < N; width = 2 * width)
    {
        #ifndef NDEBUG
            memset(b, 0xff, N*sizeof(int));
        #endif
        
        for (int i = 0; i < N; i += 2*width)
            merge_sorted(b + i, a + i, MIN(width, N - i), a + i + width, MIN(width, N - i - width));
          
        SWAP(&a, &b);
    }

    return a;
}


SORT_API void gen_merge_sorted(void* __restrict output, const void* a, isize a_len, const void* b, isize b_len, isize item_size, Is_Less_Func is_less, void* context)
{
    #define AT_OF(items, I) ((char*) (items) + (I)*item_size)
    isize ai = 0;
    isize bi = 0;
    while(ai < a_len && bi < b_len)
    {
        if(is_less(AT_OF(a, ai), AT_OF(b, bi), context))
            memcpy(AT_OF(output, ai + bi), AT_OF(a, ai++), item_size);
        else
            memcpy(AT_OF(output, ai + bi), AT_OF(b, bi++), item_size);
    }

    if(ai < a_len)
        memcpy(AT_OF(output, ai + bi), AT_OF(a, ai), (a_len - ai)*item_size);
    else
        memcpy(AT_OF(output, ai + bi), AT_OF(b, bi), (b_len - bi)*item_size);

}

SORT_API void* gen_merge_sort(void* __restrict input, void* __restrict temp, bool dont_copy_back, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    void* __restrict a = input;
    void* __restrict b = temp;


    isize N = item_count;
    for (isize i = 0; i < N; i += INSERTION_SORT_TO)
        gen_insertion_sort(AT_OF(a, i), temp, MIN(INSERTION_SORT_TO, N - i), item_size, is_less, context);

    for (isize width = INSERTION_SORT_TO; width < N; width = 2 * width)
    {
        for (isize i = 0; i < N; i += 2*width)
            gen_merge_sorted(AT_OF(b, i), 
                AT_OF(a, i), MIN(width, N - i), 
                AT_OF(a, i + width), MIN(width, N - i - width),
                item_size, is_less, context);
          
        SWAP(&a, &b);
    }
    
    if(dont_copy_back == false && a != input)
    {
        memcpy(input, a, N*item_size);
        a = input;
    }

    return a;
    #undef AT_OF
}

ATTRIBUTE_INLINE_NEVER
void merge_sort(int* __restrict input, int* __restrict temp, int N)
{
    void* out = merge_sort_no_copy_back(input, temp, N);
    if(out != input)
        memcpy(input, out, N*sizeof(int));
}

static ATTRIBUTE_INLINE_ALWAYS
void k_heap_sift_down(int arr[], int N, int root, int k)
{
    while(true) 
    {
        int first_child = root*k + 1;
        //ASSERT(first_child < N);
        if (first_child >= N) 
            return;

        int max_i = root;

        int to = first_child + k < N ? first_child + k : N;
        for(int j = first_child; j < to; j++) 
            max_i = (arr[max_i] < arr[j]) ? j : max_i;

        if (max_i == root) 
            return;

        ASSERT(root < N);
        SWAP(&arr[root], &arr[max_i]);
        root = max_i;
    }
}

static ATTRIBUTE_INLINE_ALWAYS
void k_heap_sort(int arr[], int N, int k)
{
    for(int node = N/k + 1; node-- > 0;)
        k_heap_sift_down(arr, N, node, k);

    for(int i = N; i-- > 1;) {
        ASSERT(i < N);
        SWAP(&arr[0], &arr[i]);
        k_heap_sift_down(arr, i, 0, k);
    }
}

ATTRIBUTE_INLINE_NEVER
void heap_sort_4(int arr[], int N)
{
    k_heap_sort(arr, N, 4);
}

ATTRIBUTE_INLINE_NEVER void quicksort_iter(int a[], int n) 
{
    int depth = 0;
    int los[64] = {0};
    int his[64] = {n - 1};

    for(; depth >= 0; depth--)
    {
        recurse:
        int lo = los[depth];
        int hi = his[depth];
        while (lo < hi)
        {
            int size = hi - lo + 1; 
            if(size <= 32)
            {
                pdq_insertion_sort(a + lo, size);
                break;
            }
        
            int i = lo, j = (lo + hi)/2, k = hi;
            if (a[k] < a[i]) SWAP(&a[k], &a[i]);
            if (a[j] < a[i]) SWAP(&a[j], &a[i]);
            if (a[k] < a[j]) SWAP(&a[k], &a[j]);
            int pivot = a[j];
      
            while (i <= k) {            
                while (a[i] < pivot)
                    i++;
                while (a[k] > pivot)
                    k--;
                if (i <= k) {
                    SWAP(&a[i], &a[k]);
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

ATTRIBUTE_INLINE_NEVER void quicksort_iter_register(int a[], int n) 
{
    int log2_n = 0;
    if(1)
    {
        int n_copy = n;
        while (n_copy >>= 1) ++log2_n;
    }

    int depth = 0;
    int los[64];
    int his[64];
    int unbalances[64];

    los[0] = 0;
    his[0] = n-1;
    unbalances[0] = log2_n;

    for(; depth >= 0; depth--)
    {
        ASSERT(depth < 64);
        int lo = los[depth];
        int hi = his[depth];
        int unbalanced = unbalances[depth];
        recurse:

        while (lo < hi)
        {
            int size = hi - lo + 1; 
            if(size <= ISORT_FROM)
            {
                pdq_insertion_sort(a + lo, size);
                break;
            }
        
            int i = lo, j = (lo + hi)/2, k = hi;
            if (a[k] < a[i]) SWAP(&a[k], &a[i]);
            if (a[j] < a[i]) SWAP(&a[j], &a[i]);
            if (a[k] < a[j]) SWAP(&a[k], &a[j]);

            int pivot = a[j];
            while (i <= k) {            
                while (a[i] < pivot)
                    i++;
                while (a[k] > pivot)
                    k--;
                if (i <= k) {
                    SWAP(&a[i], &a[k]);
                    i++;
                    k--;
                }
            }

            int l_size = k - lo;
            int r_size = hi - i;
            bool is_highly_unbalanced = (unsigned)l_size < (unsigned)size/8 || (unsigned)r_size < (unsigned)size/8;
            unbalanced -= is_highly_unbalanced;
            if(unbalanced <= 0)
                break;
            
            //recur to the side with fewer elements 
            //the other side is covered in the next iteration of the while loop
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

        if(unbalanced <= 0)
            heap_sort_balanced(a + lo, hi - lo + 1);
    }
}

ATTRIBUTE_INLINE_NEVER void quicksort_iter_register2(int a[], int n) 
{
    //The region [lo, hi] which we are partitioning
    int lo = 0;
    int hi = n-1;

    //we allow at maximum log2_n "highly unbalanced" (bad) paritions (see below how it is exactly calculated).
    //If we exceed that we switch to our highly optimized heapsort instead. This keeps this algorithm O(n logn) 
    // no matter the input data
    int log2_n = 0;
    {
        int n_copy = n;
        while (n_copy >>= 1) ++log2_n;
    }
    int unbalanced = log2_n;

    //explicit stack - we cannot use "real" recursion because that stops the compiler
    // from being able to inline everything to the parent function. This is bad because it
    // also stops the passed in comparison function / mempcy calls from being inlined in,
    // thus essentially going back to qsort approach 
    int depth = 0;
    int los[64];
    int his[64];
    int unbalances[64];

    //for depth >= 0
    for(;;)
    {
        recurse: 
        for(;;)
        {
            //if small ammount of items use insertion sort and "return" from this recursion
            int size = hi - lo + 1; 
            if(size <= ISORT_FROM)
            {
                pdq_insertion_sort(a + lo, size);
                break;
            }
        
            //median of tree as a pivot
            int i = lo, j = (lo + hi)/2, k = hi;
            if (a[k] < a[i]) SWAP(&a[k], &a[i]);
            if (a[j] < a[i]) SWAP(&a[j], &a[i]);
            if (a[k] < a[j]) SWAP(&a[k], &a[j]);

            //partition
            int pivot = a[j];
            while (i <= k) {            
                while (a[i] < pivot)
                    i++;
                while (a[k] > pivot)
                    k--;
                if (i <= k) {
                    SWAP(&a[i], &a[k]);
                    i++;
                    k--;
                }
            }

            //Detect unbalanced partitions (see above for why). If too many
            // More then log2_n unbalanced partitions "return" from this recursion
            // and use heap sort instead.
            int l_size = k - lo;
            int r_size = hi - i;
            bool is_highly_unbalanced = (unsigned)l_size < (unsigned)size/8 
                                     || (unsigned)r_size < (unsigned)size/8;
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
            heap_sort_balanced(a + lo, hi - lo + 1);

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

SORT_API void gen_quicksort_heapsort(void* items, void* space_for_two_items, isize heap_sort_from, isize item_count, isize item_size, Is_Less_Func is_less, void* context)
{
    //The region [lo, hi] which we are partitioning
    isize lo = 0;
    isize hi = item_count-1;

    void* pivot = space_for_two_items;
    void* swap_space = (char*) space_for_two_items + item_size;

    //we allow at maximum log2_n "highly unbalanced" (bad) paritions (see below how it is exactly calculated).
    //If we exceed that we switch to our highly optimized heapsort instead. This keeps this algorithm O(n logn) 
    // no matter the input data
    isize log2_n = 0;
    {
        isize n_copy = item_count;
        while (n_copy >>= 1) ++log2_n;
    }
    isize unbalanced = log2_n;

    //explicit stack - we cannot use "real" recursion because that stops the compiler
    // from being able to inline everything to the parent function. This is bad because it
    // also stops the passed in comparison function / mempcy calls from being inlined in,
    // thus essentially going back to qsort approach 
    isize depth = 0;
    isize los[64];
    isize his[64];
    isize unbalances[64];

    if(item_count >= heap_sort_from)
        goto big_heap_sort;

    //for depth >= 0
    for(;;)
    {
        recurse: 
        for(;;)
        {
            //if small ammount of items use insertion sort and "return" from this recursion
            isize size = hi - lo + 1; 
            if(size <= INSERTION_SORT_TO)
            {
                gen_insertion_sort(AT(lo), swap_space, size, item_size, is_less, context);
                break;
            }
        
            //median of tree as a pivot
            isize i = lo, j = (lo + hi)/2, k = hi;
            if (is_less(AT(k), AT(i), context)) SWAP_DYN(AT(k), AT(i));
            if (is_less(AT(j), AT(i), context)) SWAP_DYN(AT(j), AT(i));
            if (is_less(AT(k), AT(j), context)) SWAP_DYN(AT(k), AT(j));

            //partition
            memcpy(pivot, AT(j), item_size);
            while (i <= k) {            
                while (is_less(AT(i), pivot, context))
                    i++;
                while (is_less(pivot, AT(k), context))
                    k--;
                if (i <= k) {
                    SWAP_DYN(AT(i), AT(k));
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
            //printf("called heapsort\n");
            gen_heap_sort(AT(lo), swap_space, hi - lo + 1, item_size, is_less, context);
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

SORT_API void quicksort_iter_inline(void* items, int item_size, int item_count, void* space_for_two_items, Is_Less_Func is_less, void* context)
{
    void* swap_space = space_for_two_items;
    void* pivot = (char*) space_for_two_items + item_size;

    int depth = 0;
    int los[64]; 
    int his[64];
    los[0] = 0;
    his[0] = item_count - 1;

    bool do_insertion = false;
    for(; depth >= 0; depth--)
    {
        ASSERT(depth < 64);

        recurse:
        int lo = los[depth];
        int hi = his[depth];

        while (lo < hi)
        {
            int size = hi - lo + 1; 
            if(size <= 32)
            {
                gen_insertion_sort(AT(lo), swap_space, size, item_size, is_less, context);
                break;
            }
        
            int i = lo, j = (lo + hi)/2, k = hi;
            if (is_less(AT(k), AT(i), context)) SWAP_DYN(AT(k), AT(i));
            if (is_less(AT(j), AT(i), context)) SWAP_DYN(AT(j), AT(i));
            if (is_less(AT(k), AT(j), context)) SWAP_DYN(AT(k), AT(j));
            memcpy(pivot, AT(j), item_size);
        
            while (i <= k) {            
                while (is_less(AT(i), pivot, context))
                    i++;
                while (is_less(pivot, AT(k), context))
                    k--;
                if (i <= k) {
                    SWAP_DYN(AT(i), AT(k));
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

SORT_API void quicksort_iter(void* items, int item_size, int item_count, Is_Less_Func is_less, void* context)
{
    u64 buffer[256];
    void* ptr = buffer;
    if(2*item_size > sizeof(buffer)) ptr = malloc(2*item_size);
    quicksort_iter_inline(items, item_size, item_count, ptr, is_less, context);
    if(2*item_size > sizeof(buffer)) free(ptr);
}

ATTRIBUTE_INLINE_NEVER void quicksort_iter_int(int* items, int n)
{
    quicksort_iter(items, sizeof *items, n, int_less, NULL);
}

ATTRIBUTE_INLINE_NEVER void quicksort_heapsort_int(int* items, int n)
{
    int two[2];
    gen_quicksort_heapsort(items, two, SORT_HEAP_SORT_FROM, n, sizeof *items, int_less, NULL);
}

ATTRIBUTE_INLINE_NEVER void quicksort_generic(void* items, int item_size, int item_count, Is_Less_Func is_less, void* context)
{
    quicksort_iter(items, item_size, item_count, is_less, context);
}

void perf_do_not_optimize(const void* ptr) 
{ 
	#if defined(__GNUC__) || defined(__clang__)
		__asm__ __volatile__("" : "+r"(ptr))
	#else
		static volatile int __perf_always_zero = 0;
		if(__perf_always_zero != 0)
		{
			volatile int* vol_ptr = (volatile int*) (void*) ptr;
			//If we would use the following line the compiler could infer that 
			//we are only really modifying the value at ptr. Thus if we did 
			// perf_do_not_optimize(long_array) it would gurantee no optimize only at the first element.
			//The precise version is also not very predictable. Often the compilers decide to only keep the first element
			// of the array no metter which one we actually request not to optimize. 
			//
			// __perf_always_zero = *vol_ptr;
			__perf_always_zero = vol_ptr[*vol_ptr];
		}
	#endif
}

#include "allocator_malloc.h"
#include "log_file.h"
#include "pdqsort.h"

int main() {
    platform_init();
    arena_stack_init(scratch_arena_stack(), "scratch arena", 0, 0, 0);

    File_Logger file_logger = {0};
    file_logger_init_use(&file_logger, allocator_get_malloc(), "logs");
    
    int N = 300;
    size_t bytes = (size_t) N*sizeof(int);
    int* rand_nums = (int*) calloc(bytes, 1);
    int* temp = (int*) calloc(bytes, 1);
    int* nums = (int*) calloc(bytes, 1);
    
    for(int i = 0; i < 100; i++)
    {
        //int seed = clock();
        //int seed = 228;
        //gen_numbers(rand_nums, N, seed);
        gen_numbers(rand_nums, N, clock());
        int* std_nums = (int*) calloc(bytes, 1);
        memcpy(std_nums, rand_nums, bytes);
        std::sort(std_nums, std_nums + N);
        
        memcpy(nums, rand_nums, bytes);
        insertion_sort(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);

        memcpy(nums, rand_nums, bytes);
        pdq_insertion_sort(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);

        memcpy(nums, rand_nums, bytes);
        quicksort_iter(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        memcpy(nums, rand_nums, bytes);
        quicksort_iter_register(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);

        memcpy(nums, rand_nums, bytes);
        quicksort_iter_register2(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);

        memcpy(nums, rand_nums, bytes);
        heap_sort_std_like(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);

        memcpy(nums, rand_nums, bytes);
        heap_sort(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        memcpy(nums, rand_nums, bytes);
        heap_sort_two_swap(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        memcpy(nums, rand_nums, bytes);
        heap_sort_std_like_two_swap(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        memcpy(nums, rand_nums, bytes);
        heap_sort_balanced(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        memcpy(nums, rand_nums, bytes);
        merge_sort(nums, temp, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        memcpy(nums, rand_nums, bytes);
        quicksort_heapsort_int(nums, N);
        TEST(memcmp(std_nums, nums, bytes) == 0);
        
        //Heap functions
        int* heap1 = (int*) calloc(bytes, 1);
        int* heap2 = (int*) calloc(bytes, 1);

        //Full heaps
        memcpy(heap1, rand_nums, bytes);
        heap_make(heap1, 0, N);
        TEST(heap_is_heap(heap1, 0, N));
        heap_sort_heap(heap1, N);
        TEST(is_sorted(heap1, N));
        TEST(memcmp(std_nums, heap1, bytes) == 0);

        memcpy(heap2, rand_nums, bytes);
        for(int i = 2; i <= N; i ++)
        {
            TEST(heap_is_heap(heap2, 0, i - 1));
            heap_push_last(heap2, 0, i);
            TEST(heap_is_heap(heap2, 0, i));
        }
        heap_sort_heap(heap2, N);
        TEST(is_sorted(heap2, N));
        TEST(memcmp(std_nums, heap2, bytes) == 0);
            
        //Partial heaps
        int heap_top = N/5;
        memcpy(heap1, rand_nums, bytes);
        heap_make(heap1, heap_top, N);
        TEST(heap_is_heap(heap1, heap_top, N));
        
        memcpy(heap2, rand_nums, bytes);
        for(int i = heap_top + 2; i <= N; i ++)
        {
            TEST(heap_is_heap(heap2, heap_top, i - 1));
            heap_push_last(heap2, heap_top, i);
            TEST(heap_is_heap(heap2, heap_top, i));
        }

        free(heap1);
        free(heap2);
        free(std_nums);
    }
    
    gen_numbers(rand_nums, N, clock());
    //gen_numbers(rand_nums, N, 0);
    double time = 2;
    if(0)
    {
    log_perf_stats_hdr(log_okay(""), "logging perf        ");
    Perf_Benchmark bench_noop = {0};
	while(perf_benchmark(&bench_noop, time)) {
        memcpy(nums, rand_nums, bytes);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "noop:               ", bench_noop.inline_stats);
    }

    if(1)
    {
    
    if(1)
    {
    
    Perf_Benchmark bench_std_sort = {0};
	while(perf_benchmark(&bench_std_sort, time)) {
        memcpy(nums, rand_nums, bytes);
        std::sort(nums, nums+N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "std_sort:           ", bench_std_sort.inline_stats);

    if(0)
    {
    
    Perf_Benchmark bench_quick_sort_iter = {0};
	while(perf_benchmark(&bench_quick_sort_iter, time)) {
        memcpy(nums, rand_nums, bytes);
        quicksort_iter(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "quick_sort_iter:    ", bench_quick_sort_iter.inline_stats);
    
    Perf_Benchmark bench_quick_sort_iter_reg = {0};
	while(perf_benchmark(&bench_quick_sort_iter_reg, time)) {
        memcpy(nums, rand_nums, bytes);
        quicksort_iter_register(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "quick_sort_iter_reg: ", bench_quick_sort_iter_reg.inline_stats);
    }
    
    Perf_Benchmark bench_quick_sort_iter_reg2 = {0};
	while(perf_benchmark(&bench_quick_sort_iter_reg2, time)) {
        memcpy(nums, rand_nums, bytes);
        quicksort_iter_register2(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "quick_sort_iterreg2:", bench_quick_sort_iter_reg2.inline_stats);

    Perf_Benchmark bench_pdqsort = {0};
	while(perf_benchmark(&bench_pdqsort, time)) {
        memcpy(nums, rand_nums, bytes);
        pdqsort(nums, nums + N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "pdqsort:            ", bench_pdqsort.inline_stats);
    
    Perf_Benchmark bench_merge_sort = {0};
	while(perf_benchmark(&bench_merge_sort, time)) {
        memcpy(nums, rand_nums, bytes);
        merge_sort(nums, temp, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "merge_sort:         ", bench_merge_sort.inline_stats);
    }

    if(0)
    {
    
    Perf_Benchmark bench_heap_sort = {0};
	while(perf_benchmark(&bench_heap_sort, time)) {
        memcpy(nums, rand_nums, bytes);
        heap_sort(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort:          ", bench_heap_sort.inline_stats);
    
    Perf_Benchmark bench_heap_sort_two_step = {0};
	while(perf_benchmark(&bench_heap_sort_two_step, time)) {
        memcpy(nums, rand_nums, bytes);
        heap_sort_two_swap(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_two_step: ", bench_heap_sort_two_step.inline_stats);
    
    Perf_Benchmark bench_heap_sort_std_like = {0};
	while(perf_benchmark(&bench_heap_sort_std_like, time)) {
        memcpy(nums, rand_nums, bytes);
        heap_sort_std_like(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_std_like: ", bench_heap_sort_std_like.inline_stats);
    
    Perf_Benchmark bench_heap_sort_std_ts = {0};
	while(perf_benchmark(&bench_heap_sort_std_ts, time)) {
        memcpy(nums, rand_nums, bytes);
        heap_sort_std_like_two_swap(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_std_ts:   ", bench_heap_sort_std_ts.inline_stats);
    }
    
    Perf_Benchmark bench_heap_sort_balanced = {0};
	while(perf_benchmark(&bench_heap_sort_balanced, time)) {
        memcpy(nums, rand_nums, bytes);
        heap_sort_balanced(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_balanced: ", bench_heap_sort_balanced.inline_stats);
    

    Perf_Benchmark bench_heap_sort_std = {0};
	while(perf_benchmark(&bench_heap_sort_std, time)) {
        memcpy(nums, rand_nums, bytes);
        std::make_heap(nums, nums + N);
        std::sort_heap(nums, nums + N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_std:      ", bench_heap_sort_std.inline_stats);
    
    if(0)
    {
    Perf_Benchmark bench_heap_sort_2 = {0};
	while(perf_benchmark(&bench_heap_sort_2, time)) {
        memcpy(nums, rand_nums, bytes);
        k_heap_sort(nums, N, 2);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_2:        ", bench_heap_sort_2.inline_stats);

    Perf_Benchmark bench_heap_sort_4 = {0};
	while(perf_benchmark(&bench_heap_sort_4, time)) {
        memcpy(nums, rand_nums, bytes);
        k_heap_sort(nums, N, 4);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_4:        ", bench_heap_sort_4.inline_stats);
    
    Perf_Benchmark bench_heap_sort_8 = {0};
	while(perf_benchmark(&bench_heap_sort_8, time)) {
        memcpy(nums, rand_nums, bytes);
        k_heap_sort(nums, N, 8);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "heap_sort_8:        ", bench_heap_sort_8.inline_stats);
    }
    
    Perf_Benchmark bench_quick_sort_iter_int = {0};
	while(perf_benchmark(&bench_quick_sort_iter_int, time)) {
        memcpy(nums, rand_nums, bytes);
        quicksort_iter_int(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "quick_sort_iter_int:", bench_quick_sort_iter_int.inline_stats);
    
    Perf_Benchmark bench_quick_heap_sort_int = {0};
	while(perf_benchmark(&bench_quick_heap_sort_int, time)) {
        memcpy(nums, rand_nums, bytes);
        quicksort_heapsort_int(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "hquick_sort_int:    ", bench_quick_heap_sort_int.inline_stats);
    
    Perf_Benchmark bench_quick_sort_generic = {0};
	while(perf_benchmark(&bench_quick_sort_generic, time)) {
        memcpy(nums, rand_nums, bytes);
        quicksort_generic(nums, sizeof *nums, N, int_less, NULL);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "quick_sort_generic: ", bench_quick_sort_generic.inline_stats);

    Perf_Benchmark bench_qsort = {0};
	while(perf_benchmark(&bench_qsort, time)) {
        memcpy(nums, rand_nums, bytes);
        qsort(nums, N, sizeof *nums, int_compc_fast);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "qsort:              ", bench_qsort.inline_stats);
    }

    if(0)
    {
    Perf_Benchmark bench_insert = {0};
	while(perf_benchmark(&bench_insert, time)) {
        memcpy(nums, rand_nums, bytes);
        insertion_sort(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "insert:             ", bench_insert.inline_stats);
    
    Perf_Benchmark bench_insert_pdq = {0};
	while(perf_benchmark(&bench_insert_pdq, time)) {
        memcpy(nums, rand_nums, bytes);
        pdq_insertion_sort(nums, N);
        perf_do_not_optimize(nums);
    }
    log_perf_stats_row(log_okay(""), "insert_pdq:         ", bench_insert_pdq.inline_stats);
    }
}

#include "platform_windows.c"
