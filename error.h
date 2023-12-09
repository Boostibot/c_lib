#ifndef JOT_ERROR
#define JOT_ERROR

#include "allocator.h"
#include "array.h"
#include "assert.h"

// This file presents an unified error handling startegy in 
// the engines major systems (small functions that fail for 
// one or two reasons can still use custom booleans)

// It works by combining a static error code with a dynamically generated module id 
// with minimal required structure. This ensures the conveninece of simple error enum 
// while enabling esentially boundless dynamic behaviour. We can for example generate 
// specific modules that represent a single error with its arguments and then generate 
// a very specific error message. We can then some time later unregister this module. 
// We can also use this sytem to wrap platform errors without affecting
// the rest of the system.

// By default we register the following systems: platform (1) and stdlib (errno) (2) 
enum {
    ERROR_MODULE_PLATFORM = 1,
    ERROR_MODULE_STDLIB = 2,
};

// Represent any error from any module. 
// code holds the value of module unique error code (indexed enum). 0 always represents OK
// module holds a dynamically asigned module id. 0 represents NULL state thus the minimum value is 1
// data is any packet of module and code specific data. 
typedef struct Error {
    u32 module;
    u32 code;
    u64 data[3];
} Error;

// Translator callback used to translate errors to string values. 
// Translator should always handle all error codes potentially returning some specific text
// inidcating that the code should have never appeared. This is important because we might sometimes
// override the code field inside the Error by a mistake.
typedef const char* (*Error_Translate_Func)(u32 error_code, void* context);

typedef struct Error_Module {
    Error_Translate_Func translate_func;
    char* module_name;
    void* context;
} Error_Module;


EXPORT Error error_make(u32 module, u32 code);
EXPORT Error error_from_platform(Platform_Error error);
EXPORT Error error_from_stdlib(int error);

EXPORT bool error_is_ok(Error error); //returns wheter the error is okay ie is not error at all.
EXPORT const char* error_code(Error error); //returns the translated text of the error that occured. The returned string must not be stored anywhere! (this means either directly printed or immediately copied)
EXPORT const char* module(Error error); //returns the name of the module the error belongs to. The returned string must not be stored anywhere!

EXPORT void error_system_init(Allocator* allocator);
EXPORT void error_system_deinit();

//Returns a pointer to the Error_Module associatd with this module. If the module is not valid returns NULL.
EXPORT Error_Module* error_system_get_module(u32 module);

//Registers a new module with the given translate_func, name and context. Name does NOT need to be unique.
EXPORT u32 error_system_register_module(Error_Translate_Func translate_func, const char* module_name, void* context);
//Default translate_func for platform module
EXPORT const char* error_system_platform_translator(u32 error_code, void* context);
//Default translate_func for stdlib module
EXPORT const char* error_system_stdlib_translator(u32 error_code, void* context);

#define ERROR_SYSTEM_STRING_OK                 "OK"
#define ERROR_SYSTEM_STRING_NO_MODULE          ""
#define ERROR_SYSTEM_STRING_INVALID_MODULE     "error.h: invalid module number"
#define ERROR_SYSTEM_STRING_INVALID_TRANSLATOR "error.h: missing translate_func for module"
#define ERROR_SYSTEM_STRING_UNEXPECTED_ERROR   "Unexpected error code for this module. This is likely a result of a bug."

#define ERROR_FMT          "'%s' from module '%s'"
#define ERROR_PRINT(error) error_code(error), module(error)

#define ERROR_OK       BRACE_INIT(Error){0, 0}
#define ERROR_AND(err)  ((err).code != 0) ? (err) :  
//use like:
// Error my_error = {0};
// my_error = ERROR_AND(my_error) function_returning_error(1);
// my_error = ERROR_AND(my_error) function_returning_error(2);
// my_error = ERROR_AND(my_error) function_returning_error(3);

#endif

#define JOT_ALL_IMPL

#if (defined(JOT_ALL_IMPL) || defined(JOT_ERROR_IMPL)) && !defined(JOT_ERROR_HAS_IMPL)
#define JOT_ERROR_HAS_IMPL


EXPORT bool error_is_ok(Error error)
{
    return error.code == 0;
}

EXPORT const char* error_system_platform_translator(u32 error_code, void* context)
{
    (void) context;
    return platform_translate_error((Platform_Error) error_code);
}

EXPORT const char* error_system_stdlib_translator(u32 error_code, void* context)
{
    (void) context;
    return strerror((int) error_code);
}

//FUnction used to mark deleted modules. Should not get ever called.
EXPORT const char* error_system_gravestone_translator(u32 error_code, void* context)
{
    ASSERT(false);
    (void) context;
    (void) error_code;
    return "<gravestone>";
}

EXPORT Error error_make(u32 module, u32 code)
{
    Error result = {module, (u32) code};
    return result;
}

EXPORT Error error_from_platform(Platform_Error error)
{
    return error_make(ERROR_MODULE_PLATFORM, (u32) error);
}

EXPORT Error error_from_stdlib(int error)
{
    return error_make(ERROR_MODULE_STDLIB, (u32) error);
}

DEFINE_ARRAY_TYPE(Error_Module, Error_Module_Array);

typedef struct _Error_System
{
    Allocator* allocator;
    Error_Module_Array modules;

    bool is_init;
} _Error_System;

static _Error_System global_error_system = {0};

EXPORT const char* error_code(Error error)
{
    if(error.code == 0)
        return ERROR_SYSTEM_STRING_OK;
        
    Error_Module* registered = error_system_get_module(error.module);
    if(registered == NULL)
        return ERROR_SYSTEM_STRING_INVALID_MODULE;

    if(registered->translate_func == NULL)
        return ERROR_SYSTEM_STRING_INVALID_TRANSLATOR;

    const char* error_code = registered->translate_func(error.code, registered->context);
    return error_code;
}

EXPORT const char* module(Error error)
{
    if(error.module == 0)
        return ERROR_SYSTEM_STRING_NO_MODULE;

    Error_Module* registered = error_system_get_module(error.module);
    if(registered == NULL)
        return ERROR_SYSTEM_STRING_INVALID_MODULE;

    const char* module = registered->module_name;
    return module;
}

EXPORT void error_system_init(Allocator* allocator)
{
    error_system_deinit();

    if(allocator == NULL)
        allocator = allocator_get_default();

    array_init(&global_error_system.modules, allocator);
    array_reserve(&global_error_system.modules, 16); 

    global_error_system.allocator = allocator;
    global_error_system.is_init = true;

    error_system_register_module(error_system_platform_translator, "platform.h", NULL);
    error_system_register_module(error_system_stdlib_translator, "stdlib", NULL);
}

INTERNAL void _error_system_unregister_module(u32 module)
{
    if(module == ERROR_MODULE_STDLIB || module == ERROR_MODULE_PLATFORM)
        return;

    Error_Module* registered = error_system_get_module(module);
    if(registered == false)
        return;

    isize len = registered->module_name ? (isize) strlen(registered->module_name) : 0;
    allocator_deallocate(global_error_system.allocator, registered->module_name, len + 1, DEF_ALIGN, SOURCE_INFO());
    memset(registered, 0, sizeof *registered);
    registered->translate_func = error_system_gravestone_translator; 
}

EXPORT void error_system_deinit()
{
    if(global_error_system.is_init == false)
        return;

    for(isize i = 0; i < global_error_system.modules.size; i++)
        _error_system_unregister_module((u32) i + 1);
        
    array_deinit(&global_error_system.modules);
    memset(&global_error_system, 0, sizeof global_error_system);
}

//returns a pointer to the _Error_System associatd with this module. If the module is not valid returns NULL.
EXPORT Error_Module* error_system_get_module(u32 module)
{
    if(module == 0 || module > global_error_system.modules.size)
        return NULL;

    Error_Module* wrapper = &global_error_system.modules.data[module - 1];
    if(wrapper->translate_func == error_system_gravestone_translator)
        return NULL;
    else
        return wrapper;
}

//Registers a new module with the given translate_func, name and context. Name does NOT need to be unique.
EXPORT u32 error_system_register_module(Error_Translate_Func translate_func, const char* module_name, void* context)
{
    ASSERT_MSG(global_error_system.is_init, "error module must be init!");

    if(module_name == NULL)
        module_name = "";
    
    size_t len = strlen(module_name);
    Error_Module created = {0};
    created.translate_func = translate_func;
    created.module_name = (char*) allocator_allocate(global_error_system.allocator, (isize) len, DEF_ALIGN, SOURCE_INFO());
    memcpy(created.module_name, module_name, len);
    created.module_name[len] = '\0';
    created.context = context;
    array_push(&global_error_system.modules, created);
    return (u32) global_error_system.modules.size;
}

INTERNAL void error_system_unregister_module(u32 module)
{
    if(module == ERROR_MODULE_STDLIB || module == ERROR_MODULE_PLATFORM)
        return;

    _error_system_unregister_module(module);
}

#endif