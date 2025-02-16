#include "../allocator_debug.h"
#include "../time.h"
#include "../random.h"

#include <math.h>
INTERNAL void test_debug_allocator(double time)
{
    //this might be a little weird but we debug the debug allocator with itself.
    Debug_Allocator debugdebug = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_CONTINUOUS);
    {
        enum {MAX_COUNT = 10000};
        void** allocs = ALLOCATE(debugdebug.alloc, MAX_COUNT, void*);
        isize* sizes  = ALLOCATE(debugdebug.alloc, MAX_COUNT, isize);
        isize* aligns = ALLOCATE(debugdebug.alloc, MAX_COUNT, isize);

        isize iter = 0;
        for(double start = clock_sec(); clock_sec() - start < time; ) {
            iter += 1;
            Debug_Allocator debug = debug_allocator_make(debugdebug.alloc, 0);

            isize allocate_count = random_range(1, MAX_COUNT);
            isize reallocate_count = random_range(0, allocate_count);
            isize deallocate_count = random_range(0, allocate_count);

            for(isize j = 0; j < allocate_count; j++) {
                double float_size = exp2(random_range_f64(-5, 10)) * random_range_f64(1 - 0.05, 1 + 0.05) - 1;
                float_size = MAX(float_size, 0);
            
                sizes[j] = (isize) float_size;
                aligns[j] = 1ll << random_range(0, 6);
                allocs[j] = debug_allocator_func(debug.alloc, ALLOCATOR_MODE_ALLOC, sizes[j], NULL, 0, aligns[j], NULL);
            }

            for(isize j = 0; j < reallocate_count; j++) {
                double float_size = exp2(random_range_f64(-5, 10)) * random_range_f64(1 - 0.05, 1 + 0.05) - 1;
                float_size = MAX(float_size, 0);
            
                isize new_size = (isize) float_size;
    
                allocs[j] = debug_allocator_func(debug.alloc, ALLOCATOR_MODE_ALLOC, new_size, allocs[j], sizes[j], aligns[j], NULL);
                sizes[j] = new_size;
            }
            
            for(isize j = 0; j < deallocate_count; j++) {
                double float_size = exp2(random_range_f64(-5, 10)) * random_range_f64(1 - 0.05, 1 + 0.05) - 1;
                float_size = MAX(float_size, 0);
            
                debug_allocator_func(debug.alloc, ALLOCATOR_MODE_ALLOC, 0, allocs[j], sizes[j], aligns[j], NULL);
            }

            debug_allocator_deinit(&debug);
            int k = 0; k = k + 1;
        }
        
        DEALLOCATE(debugdebug.alloc, allocs, MAX_COUNT, void*);
        DEALLOCATE(debugdebug.alloc, sizes, MAX_COUNT, isize);
        DEALLOCATE(debugdebug.alloc, aligns, MAX_COUNT, isize);
    }
    debug_allocator_deinit(&debugdebug);
}
