#ifndef LIB_ERROR
#define LIB_ERROR

#include "allocator.h"
#include "string.h"
#include "parse.h"

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
    PLATFORM_ERROR = 1,
    STDLIB_ERROR = 2,
};

// Represent any error from any module. 
// code holds the value of module unique error code (indexed enum). 0 always represents OK
// module holds a dynamically asigned module id. 0 represents NULL state thus the minimum value is 1
typedef struct Error
{
    u32 module;
    u32 code;
} Error;

// Translator callback used to translate errors to string values. 
// Translator should always handle all error codes potentially returning some specific text
// inidcating that the code should have never appeared. This is important because we might sometimes
// override the code field inside the Error by a mistake.
typedef String (*Error_Translator)(u32 error_code, void* context);

typedef struct Error_Module
{
    Error_Translator translator;
    String_Builder module_name;
    void* context;
} Error_Module;

STATIC_ASSERT(sizeof(errno_t) == sizeof(u32)); //"error size must be compatible!"

#define ERROR_SYSTEM_STRING_OK                 STRING("OK")
#define ERROR_SYSTEM_STRING_NO_MODULE          STRING("")
#define ERROR_SYSTEM_STRING_INVALID_MODULE     STRING("error.h: invalid module number")
#define ERROR_SYSTEM_STRING_INVALID_TRANSLATOR STRING("error.h: missing translator for module")
#define ERROR_SYSTEM_STRING_UNEXPECTED_ERROR   STRING("Unexpected error code for this module. This is likely a result of a bug.")

EXPORT Error error_make(u32 module, u32 code);
EXPORT Error error_from_platform(Platform_Error error);
EXPORT Error error_from_stdlib(errno_t error);

EXPORT bool error_is_ok(Error error); //returns wheter the error is okay ie is not error at all.
EXPORT String error_code(Error error); //returns the translated text of the error that occured. The returned string must not be stored anywhere! (this means either directly printed or immediately copied)
EXPORT String error_module(Error error); //returns the name of the module the error belongs to. The returned string must not be stored anywhere!

EXPORT void error_code_into(String_Builder* into, Error error);
EXPORT void error_module_into(String_Builder* into, Error error);

#define ERROR_OK       BRACE_INIT(Error){0, 0}
#define ERROR_OR(err)  ((err).code != 0) ? (err) : 
//use like:
// Error my_error = {0};
// my_error = ERROR_OR(my_error) function_returning_error(1);
// my_error = ERROR_OR(my_error) function_returning_error(2);
// my_error = ERROR_OR(my_error) function_returning_error(3);

#define _STRING_FMT "%.*s"
#define _STRING_PRINT(string) (string).size, (string).data

#define ERROR_FMT          _STRING_FMT " from module " _STRING_FMT
#define ERROR_PRINT(error) _STRING_PRINT(error_code(error)), _STRING_PRINT(error_module(error))

EXPORT void error_system_init(Allocator* allocator);
EXPORT void error_system_deinit();

//Returns a pointer to the Error_Module associatd with this module. If the module is not valid returns NULL.
EXPORT Error_Module* error_system_get_module(u32 module);

//Registers a new module with the given translator, name and context. Name does NOT need to be unique.
EXPORT u32 error_system_register_module(Error_Translator translator, String module_name, void* context);
//Default translator for platform module
EXPORT String error_system_platform_translator(u32 error_code, void* context);
//Default translator for stdlib module
EXPORT String error_system_stdlib_translator(u32 error_code, void* context);

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_ERROR_IMPL)) && !defined(LIB_ERROR_HAS_IMPL)
#define LIB_ERROR_HAS_IMPL

enum {
    _ERROR_SYSTEM_MAX_PLATFORM_ERRORS = 4,
    _ERROR_SYSTEM_LOCAL_MODULES = 16,
};

typedef struct _Error_Module
{
    Error_Module registered;
    bool is_gravestone;
} _Error_Module;

DEFINE_ARRAY_TYPE(_Error_Module, _Error_Module_Array);

typedef struct _Error_System
{
    Allocator* allocator;
    _Error_Module_Array modules;
    _Error_Module local_modules[_ERROR_SYSTEM_LOCAL_MODULES];

    bool is_init;
} _Error_System;

static _Error_System global_error_system = {0};

EXPORT bool error_is_ok(Error error)
{
    return error.code == 0;
}

EXPORT String error_code(Error error)
{
    if(error.code == 0)
        return ERROR_SYSTEM_STRING_OK;
        
    Error_Module* registered = error_system_get_module(error.module);
    if(registered == NULL)
        return ERROR_SYSTEM_STRING_INVALID_MODULE;

    if(registered->translator == NULL)
        return ERROR_SYSTEM_STRING_INVALID_TRANSLATOR;

    String error_code = registered->translator(error.code, registered->context);
    error_code = string_trim_whitespace(error_code);

    return error_code;
}

EXPORT String error_module(Error error)
{
    if(error.module == 0)
        return ERROR_SYSTEM_STRING_NO_MODULE;

    Error_Module* registered = error_system_get_module(error.module);
    if(registered == NULL)
        return ERROR_SYSTEM_STRING_INVALID_MODULE;

    String error_module = string_from_builder(registered->module_name);
    error_module = string_trim_whitespace(error_module);

    return error_module;
}

EXPORT void error_code_into(String_Builder* into, Error error)
{
    array_clear(into);
    builder_append(into, error_code(error));
}

EXPORT void error_module_into(String_Builder* into, Error error)
{
    array_clear(into);
    builder_append(into, error_module(error));
}

EXPORT void error_system_init(Allocator* allocator)
{
    error_system_deinit();

    global_error_system.allocator = allocator;
    array_init_backed_from_memory(&global_error_system.modules, allocator, global_error_system.local_modules, _ERROR_SYSTEM_LOCAL_MODULES);
    
    global_error_system.is_init = true;

    error_system_register_module(error_system_platform_translator, STRING("platform.h"), NULL);
    error_system_register_module(error_system_stdlib_translator, STRING("stdlib"), NULL);

}

INTERNAL void _error_system_unregister_module(u32 module)
{
    if(module == STDLIB_ERROR || module == PLATFORM_ERROR)
        return;

    Error_Module* registered = error_system_get_module(module);
    if(registered == false)
        return;

    array_deinit(&registered->module_name);
    memset(registered, 0, sizeof *registered);
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

    _Error_Module* wrapper = &global_error_system.modules.data[module - 1];
    if(wrapper->is_gravestone)
        return NULL;
    else
        return &wrapper->registered;
}

//Registers a new module with the given translator, name and context. Name does NOT need to be unique.
EXPORT u32 error_system_register_module(Error_Translator translator, String module_name, void* context)
{
    ASSERT(global_error_system.is_init && "error module must be init!");

    _Error_Module created = {0};
    created.is_gravestone = false;
    created.registered.translator = translator;
    created.registered.module_name = builder_from_string(module_name, global_error_system.allocator);
    created.registered.context = context;
    array_push(&global_error_system.modules, created);
    return (u32) global_error_system.modules.size;
}

INTERNAL void error_system_unregister_module(u32 module)
{
    if(module == STDLIB_ERROR || module == PLATFORM_ERROR)
        return;

    _error_system_unregister_module(module);
}


EXPORT String error_system_platform_translator(u32 error_code, void* context)
{
    (void) context;
    return string_make(platform_translate_error((Platform_Error) error_code));
}

EXPORT String error_system_stdlib_translator(u32 error_code, void* context)
{
    (void) context;
    return string_make(strerror((int) error_code));
}

EXPORT Error error_make(u32 module, u32 code)
{
    Error result = {module, (u32) code};
    return result;
}

EXPORT Error error_from_platform(Platform_Error error)
{
    return error_make(PLATFORM_ERROR, (u32) error);
}

EXPORT Error error_from_stdlib(errno_t error)
{
    return error_make(STDLIB_ERROR, (u32) error);
}

#endif