#if !defined(JOT_profile_defs) 
#define JOT_profile_defs
#include <stdint.h>

typedef enum Profile_Type {
	PROFILE_UNINIT = 0,
	PROFILE_COUNTER,
	PROFILE_FAST,
	PROFILE_DEFAULT,
} Profile_Type;

typedef struct Profile_ID {
	Profile_Type type;
	int line;
	const char* file;
	const char* function;
	const char* name;
	const char* comment;
} Profile_ID;


#if defined(_MSC_VER)
    #define _PROFILE_THREAD_LOCAL  __declspec(thread)
    #define _PROFILE_INLINE_ALWAYS                                 __forceinline
#else
	#define _PROFILE_THREAD_LOCAL  __thread
    #define _PROFILE_INLINE_ALWAYS                                 __attribute__((always_inline)) inline
#endif

typedef struct Profile_Thread_Zone Profile_Thread_Zone;
static _PROFILE_INLINE_ALWAYS void profile_submit(Profile_Type type, Profile_Thread_Zone** handle_ptr, const Profile_ID* zone_id, int64_t before, int64_t after);
static _PROFILE_INLINE_ALWAYS int64_t profile_now();


#ifndef DO_PROFILE
	#define DO_PROFILE 1
#endif

#if DO_PROFILE
	#define _PROFILE_START(TYPE, id, comment, ...) \
		static Profile_ID __prof_id_##id = {TYPE, __LINE__, __FILE__, __func__, #id, "" comment}; \
		static _PROFILE_THREAD_LOCAL Profile_Thread_Zone* __prof_handle_##id = 0; \
		int64_t __prof_before_##id = profile_now() \
	
	#define _PROFILE_END(TYPE, id, ...) \
		profile_submit(TYPE, &__prof_handle_##id, &__prof_id_##id, __prof_before_##id, profile_now()) \

#else
	#define _PROFILE_START(TYPE, id, comment, ...) 
	#define _PROFILE_END(TYPE, id, ...) 
#endif

#define PROFILE_START(...) _PROFILE_START(PROFILE_DEFAULT,__VA_ARGS__,,,)
#define PROFILE_END(...) _PROFILE_END(PROFILE_DEFAULT,__VA_ARGS__,,,)

#define PROFILE_FSTART(...) _PROFILE_START(PROFILE_FAST,__VA_ARGS__,,,)
#define PROFILE_FEND(...) _PROFILE_END(PROFILE_FAST,__VA_ARGS__,,,)

#define PROFILE_COUNTER(...) do { \
		_PROFILE_START(PROFILE_COUNTER,__VA_ARGS__,,,); \
		_PROFILE_END(PROFILE_COUNTER,__VA_ARGS__,,,); \
	} while(0) \

#endif