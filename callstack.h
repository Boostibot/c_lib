#ifndef LIB_LOG_MISC
#define LIB_LOG_MISC

#include "log.h"
#include "platform.h"
#include "string.h"

#define STACK_TRACE_FMT "%-30s " SOURCE_INFO_FMT
#define STACK_TRACE_PRINT(trace) cstring_escape((trace).function), SOURCE_INFO_PRINT(trace)

typedef Platform_Stack_Trace_Entry Stack_Trace_Entry;
DEFINE_ARRAY_TYPE(Stack_Trace_Entry, Stack_Trace);

EXPORT void callstack_capture_and_translate(Stack_Trace* into, isize depth, isize skip);
EXPORT void callstack_capture(ptr_Array* callstack, isize depth, isize skip);
EXPORT void callstack_translate(Stack_Trace* into, const void** callstack, isize callstack_size);

EXPORT void log_callstack(const char* log_module, Log_Type log_type, isize depth, isize skip);
EXPORT void log_captured_callstack(const char* log_module, Log_Type log_type, const void** callstack, isize callstack_size);

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_LOG_MISC_IMPL)) && !defined(LIB_LOG_MISC_HAS_IMPL)
#define LIB_LOG_MISC_HAS_IMPL

enum { _DEF_CALLSTACK_SIZE = 64 };

EXPORT void callstack_capture(ptr_Array* callstack, isize depth, isize skip)
{
    
    isize captured_depth = 0;
    if(depth >= 0)
    {
        array_resize(callstack, depth);
        captured_depth = platform_capture_call_stack(callstack->data, callstack->size, skip + 1);
    }
    else
    {
        depth = MAX(depth, _DEF_CALLSTACK_SIZE);
        for(isize tries = 0; tries < 4; tries ++)
        {
            array_resize(callstack, depth);
            captured_depth = platform_capture_call_stack(callstack->data, callstack->size, skip + 1);
            if(captured_depth < depth)
                break;

            depth *= 2;
        }
    }
}

EXPORT void callstack_translate(Stack_Trace* into, const void** callstack, isize callstack_size)
{
    array_resize(into, callstack_size);
    platform_translate_call_stack(into->data, callstack, callstack_size);
}

EXPORT void callstack_capture_and_translate(Stack_Trace* into, isize depth, isize skip)
{
    ptr_Array stack = {0};
    array_init_backed(&stack, allocator_get_scratch(), _DEF_CALLSTACK_SIZE);
    callstack_capture(&stack, depth, skip);
    callstack_translate(into, (const void**) stack.data, stack.size);
    array_deinit(&stack);
}


#endif