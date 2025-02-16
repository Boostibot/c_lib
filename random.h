#ifndef MODULE_RANDOM
#define MODULE_RANDOM

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef ASSERT
	#include <assert.h>
	#define ASSERT(x) assert(x)
	#define REQUIRE(x) assert(x)
#endif

#ifndef EXTERNAL
    #define EXTERNAL
#endif

EXTERNAL bool     random_bool(); //generates random bool
EXTERNAL float    random_f32(); //generates random float in range [0, 1)
EXTERNAL double   random_f64(); //generates random double in range [0, 1)
EXTERNAL uint64_t random_u64(); //generates random u64 in range [0, U64_MAX] 
EXTERNAL int64_t  random_i64(); //generates random i64 in range [I64_MIN, U64_MAX] 
EXTERNAL int64_t  random_range(int64_t from, int64_t to); //generates unbiased random integer in range [from, to)
EXTERNAL double   random_range_f64(double from, double to); 
EXTERNAL float    random_range_f32(float from, float to); 

EXTERNAL double   random_bits_to_f64(uint64_t random);
EXTERNAL float    random_bits_to_f32(uint32_t random);

EXTERNAL void     random_bytes(void* into, int64_t size); //writes size bytes of random data into into
EXTERNAL void     random_shuffle(void* elements, int64_t element_count, int64_t element_size); //Randomly shuffles the provided array
EXTERNAL void     random_swap_any(void* a, void* b, int64_t size); //Swaps two values of the given size

typedef struct Random_State {
	uint64_t state[4];
} Random_State;

EXTERNAL uint64_t     random_seed(); //Generates a random seed using system clock and this threads id
EXTERNAL Random_State random_state_make(uint64_t seed); //initializes Random_State using given seed
EXTERNAL Random_State* random_state(); //Returns pointer to the global state currently used

EXTERNAL bool     random_bool_from(Random_State* state); //generates random bool
EXTERNAL float    random_f32_from(Random_State* state); //generates random float in range [0, 1)
EXTERNAL double   random_f64_from(Random_State* state); //generates random double in range [0, 1)
EXTERNAL uint64_t random_u64_from(Random_State* state); //generates random u64 in range [0, U64_MAX] 
EXTERNAL int64_t  random_i64_from(Random_State* state); //generates random i64 in range [I64_MIN, U64_MAX] 
EXTERNAL int64_t  random_range_from(Random_State* state, int64_t from, int64_t to); //generates unbiased random integer in range [from, to)
EXTERNAL double   random_range_f64_from(Random_State* state, double from, double to); 
EXTERNAL float    random_range_f32_from(Random_State* state, float from, float to); 

//Randomly shuffles the provided array
EXTERNAL void     random_shuffle_from(Random_State* state, void* elements, int64_t element_count, int64_t element_size); 
//fill the given memory with random bytes
EXTERNAL void	  random_bytes_from(Random_State* state, void* into, int64_t size);

typedef struct Discrete_Distribution{
    int64_t value;			//set by user. This is what gets returned.
    int64_t chance;				//set by user. 
    int64_t _chance_cumulative; //set in random_discrete_make()
} Discrete_Distribution;

//Fills the remaining values of Discrete_Distribution
EXTERNAL void    random_discrete_make(Discrete_Distribution distribution[], int64_t distribution_size);
//Samples the discrete random distribution using provided state. Returns value.
EXTERNAL int64_t random_discrete_from(Random_State* state, const Discrete_Distribution distribution[], int64_t distribution_size);
//Samples the discrete random distribution using global state. Returns value.
EXTERNAL int64_t random_discrete(const Discrete_Distribution distribution[], int64_t distribution_size);

//Generates next random value
//Seed can be any value
//Taken from: https://prng.di.unimi.it/splitmix64.c
static inline uint64_t random_splitmix(uint64_t* state) 
{
	uint64_t z = (*state += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

//the xiroshiro256 algorithm
//Seed must not be anywhere zero
//Taken from: https://prng.di.unimi.it/xoshiro256plusplus.c
static inline uint64_t random_xiroshiro256(uint64_t s[4]) 
{
	#define ROTL(x, k) (((x) << (k)) | ((x) >> (64 - (k))))

	const uint64_t result = ROTL(s[0] + s[3], 23) + s[0];
	const uint64_t t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;
	s[3] = ROTL(s[3], 45);

	return result;
	#undef ROTL
}

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_RANDOM)) && !defined(MODULE_HAS_IMPL_RANDOM)
	#define MODULE_HAS_IMPL_RANDOM

	#if defined(__GNUC__) || defined(__clang__)
		#define _RAND_THREAD_LOCAL __thread
		inline static uint32_t _count_leading_zeros(uint64_t number)
		{
			return (uint32_t) __builtin_clzll(number);
		}

		#include <time.h>
		inline static uint64_t _precise_clock_time()
		{
			struct timespec ts;
			(void) clock_gettime(CLOCK_REALTIME, &ts);
			return (uint64_t) ts.tv_nsec + (uint64_t) ts.tv_sec * 1000000000LL;
		}
	#elif defined(_MSC_VER)
		#define _RAND_THREAD_LOCAL __declspec(thread)

		#include <intrin.h>
		inline static uint32_t _count_leading_zeros(uint64_t number)
		{
			unsigned long index = 0;
			_BitScanReverse64(&index, number);
			return (uint32_t) (63 - index);
		}

		inline static uint64_t _precise_clock_time()
		{
			#if defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64) 
				return (uint64_t) __rdtsc();
			#else
				typedef int BOOL;
				typedef union _LARGE_INTEGER LARGE_INTEGER;
				BOOL __stdcall QueryPerformanceCounter(LARGE_INTEGER* ticks);
				
				uint64_t now = 0;
				QueryPerformanceCounter((LARGE_INTEGER*) (void*) &now);
				return (uint64_t) now;
			#endif
		}

	#else
		#define _RAND_THREAD_LOCAL
		inline static uint32_t _count_leading_zeros(uint64_t number);
		inline static uint64_t _precise_clock_time()

		#error "add a custom implementation for this compiler!"
	#endif

	EXTERNAL double random_bits_to_f64(uint64_t random)
	{
		return (double) (random >> 11) * 0x1.0p-53;
	}
	EXTERNAL float random_bits_to_f32(uint32_t random)
	{
		return (float) (random >> 8) * 0x1.0p-23f;
	}

	EXTERNAL float random_f32_from(Random_State* state)
	{
		uint64_t random = random_xiroshiro256(state->state);
		return random_bits_to_f32((uint32_t) (random >> 32 ^ random));
	}

	EXTERNAL double random_f64_from(Random_State* state)
	{
		uint64_t random = random_xiroshiro256(state->state);
		return random_bits_to_f64(random);
	}

	EXTERNAL bool random_bool_from(Random_State* state)
	{
		int64_t random = (int64_t) random_xiroshiro256(state->state);
		return random < 0;
	}
	
	EXTERNAL int64_t random_i64_from(Random_State* state)
	{
		int64_t random_i64 = (int64_t) random_xiroshiro256(state->state);
		return random_i64;
	}

	EXTERNAL uint64_t random_u64_from(Random_State* state)
	{
		uint64_t random_u64 = random_xiroshiro256(state->state);
		return random_u64;
	}

	inline static uint64_t _random_bounded(Random_State* state, uint64_t range) 
	{
		--range;
		uint64_t index = _count_leading_zeros(range | 1);
		uint64_t mask = (uint64_t) -1 >> index;
		uint64_t x = 0;
		do 
		{
			x = random_xiroshiro256(state->state) & mask;
		} 
		while (x > range);
		return x;
	}

	EXTERNAL int64_t random_range_from(Random_State* state, int64_t from, int64_t to)
	{
		int64_t out = from;
		if(from < to)
		{
			uint64_t range = (uint64_t) (to - from);
			uint64_t bounded = _random_bounded(state, range);
			out = (int64_t) bounded + from;
		}
		return out;
	}

	//This function generates random nondeterministic seed using a sequence of hacks.
	//The reasoning is as follows:
	// 1. we want to use precise time to get nondeterminism
	// 2. we want to include the calling threads id to guarantee no two threads will get the same seed.
	// 3. we want the function to always return distinct numbers even when called in rapid succession
	//    from the same thread. Notably when the precise time is not so precise we could risk its
	//    value not changing between the calls.
	//
	// We start off with a simple counter satisfying 1.
	//
	// Then we satisfy 3. by keeping a thread local counter which gets increased on each call.
	// We add this counter to the current time thus making up for the possible lack of precision.
	// You can verify that this indeed satisfies 3 and does cause very little problems. The worst thing 
	// that can happen is that both the clock and the counter increase at the same rate, making the clock
	// iterate only half the possible numbers. This is in itself not too problematic since the clock 
	// realistically never makes one full revolutions around the u64 range.
	// 
	// Next we satisfy 2. by getting an address of thread local variable and hashing it. 
	// This gives some thread unique hash with bits spread all over. We simply xor this with our value 
	// from the previous points.
	// 
	// Last we hash everything to make the final output appear lot more random - without it the 
	// random_seed simply counts up at random intervals. This last step is optional.
	//
	// Note that random_splitmix also happens to be a lovely hash function and whats more its 
	// bijective - this means we cant run into hash collisions thus dont lose any information when hashing.
	EXTERNAL uint64_t random_seed()
	{
		uint64_t now = _precise_clock_time();
		static _RAND_THREAD_LOCAL uint64_t thread_hash = 0;
		static _RAND_THREAD_LOCAL uint64_t local = 0;
		if(local == 0)
		{
			thread_hash = (uint64_t) &local;
			thread_hash = random_splitmix(&thread_hash);
		}

		uint64_t out = (now + local) ^ thread_hash;
		out = random_splitmix(&out);
		local += 1;
		return out;
	}
	
	EXTERNAL Random_State random_state_make(uint64_t seed)
	{
		Random_State out = {0};
		out.state[0] = seed;
		out.state[1] = random_splitmix(out.state);
		out.state[2] = random_splitmix(out.state);
		out.state[3] = random_splitmix(out.state);
		return out;
	}
	
	EXTERNAL Random_State* random_state()
	{
		static _RAND_THREAD_LOCAL Random_State _random_state = {0};
		Random_State* state = &_random_state;
		if(state->state[0] == 0)
			*state = random_state_make(random_seed());

		return state;
	}
	
	EXTERNAL bool     random_bool() { return random_bool_from(random_state()); }	
	EXTERNAL float    random_f32()  { return random_f32_from(random_state()); }
	EXTERNAL double   random_f64()  { return random_f64_from(random_state()); } 
	EXTERNAL uint64_t random_u64()  { return random_u64_from(random_state()); }  
	EXTERNAL int64_t  random_i64()  { return random_i64_from(random_state()); }  

	EXTERNAL int64_t random_range(int64_t from, int64_t to) 
	{ 
		return random_range_from(random_state(), from, to); 
	}
	
	EXTERNAL double random_range_f64(double from, double to)
	{
		double range = to - from;
		double random = random_f64();
		return random*range + from;
	}

	EXTERNAL float random_range_f32(float from, float to)
	{
		float range = to - from;
		float random = random_f32();
		return random*range + from;
	}

	EXTERNAL void random_shuffle(void* elements, int64_t element_count, int64_t element_size) 
	{ 
		random_shuffle_from(random_state(), elements, element_count, element_size); 
	}

	EXTERNAL void random_bytes(void* into, int64_t size)
	{
		random_bytes_from(random_state(), into, size);
	}

	EXTERNAL void random_swap_any(void* a, void* b, int64_t size)
	{
		REQUIRE(size >= 0);
		enum {LOCAL = 16};
		char temp[LOCAL] = {0};
	
		int64_t repeats = size / LOCAL;
		int64_t remainder = size % LOCAL;
		int64_t exact = size - remainder;

		char* elemsi = (char*) a;
		char* elemsj = (char*) b;

		for(int64_t k = 0; k < repeats; k ++)
		{
			memcpy(temp,             elemsi + k*LOCAL, LOCAL);
			memcpy(elemsi + k*LOCAL, elemsj + k*LOCAL, LOCAL);
			memcpy(elemsj + k*LOCAL, temp,             LOCAL);
		}
			
		memcpy(temp,             elemsi + exact, (size_t) remainder);
		memcpy(elemsi + exact,   elemsj + exact, (size_t) remainder);
		memcpy(elemsj + exact,   temp,           (size_t) remainder);
	}

	EXTERNAL void random_shuffle_from(Random_State* state, void* elements, int64_t element_count, int64_t element_size)
	{
		REQUIRE(element_count >= 0 && element_size >= 0);
		enum {LOCAL = 256};
		char temp[LOCAL] = {0};
		char* elems = (char*) elements;
		int64_t s = element_size;

		if(element_size <= LOCAL)
		{
			for (int64_t i = 0; i < element_count - 1; i++)
			{
				int64_t offset = (int64_t) _random_bounded(state, (uint64_t) (element_count - i));
				int64_t j = offset + i;

				memcpy(temp,        elems + i*s, (size_t) s);
				memcpy(elems + i*s, elems + j*s, (size_t) s);
				memcpy(elems + j*s, temp,        (size_t) s);
			}
		}
		else
		{
			for (int64_t i = 0; i < element_count - 1; i++)
			{
				int64_t offset = (int64_t) _random_bounded(state, (uint64_t) (element_count - i));
				int64_t j = offset +  i;

				random_swap_any(elems + i*s, elems + j*s, s);
			}
		}
	}
	
	EXTERNAL void random_bytes_from(Random_State* state, void* into, int64_t size)
	{
		REQUIRE(size >= 0);
		uint64_t full_randoms = (uint64_t) size / 8;
		uint64_t remainder = (uint64_t) size % 8;
		uint64_t* fulls = (uint64_t*) into;
	
		for(uint64_t i = 0; i < full_randoms; i++)
			fulls[i] = random_u64_from(state);

		if(remainder)
		{
			union {
				uint64_t val; 
				char bytes[8];
			} last = {random_u64_from(state)};	

			char* bytes = (char*) into + full_randoms*sizeof(uint64_t);
			for(uint64_t i = 0; i < remainder; i++)
				bytes[i] = last.bytes[i];
		}
	}

	EXTERNAL void random_discrete_make(Discrete_Distribution distribution[], int64_t distribution_size)
	{
		int64_t _chance_cumulative = 0;
		for(int64_t i = 0; i < distribution_size; i++)
		{
			_chance_cumulative += distribution[i].chance;
			distribution[i]._chance_cumulative = _chance_cumulative;
		}
	}

	EXTERNAL int64_t random_discrete_from(Random_State* state, const Discrete_Distribution distribution[], int64_t distribution_size)
	{
		if(distribution_size <= 0)  
			return 0;

		int64_t range_lo = distribution[0]._chance_cumulative;
		int64_t range_hi = distribution[distribution_size - 1]._chance_cumulative;
		int64_t random = random_range_from(state, range_lo, range_hi);

		int64_t low_i = 0;
		int64_t count = distribution_size;

		while (count > 0)
		{
			int64_t step = count / 2;
			int64_t curr = low_i + step;
			if(distribution[curr]._chance_cumulative < random)
			{
				low_i = curr + 1;
				count -= step + 1;
			}
			else
				count = step;
		}
		
		ASSERT(0 <= low_i && low_i < distribution_size);
		int64_t value = distribution[low_i].value;
		return value;
	}

	EXTERNAL int64_t random_discrete(const Discrete_Distribution distribution[], int64_t distribution_size)
	{
		return random_discrete_from(random_state(), distribution, distribution_size);
	}
#endif