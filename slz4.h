#ifndef JOT_SLZ4
#define JOT_SLZ4

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef SLZ4_CUSTOM
    #include <stdlib.h>
    #include <assert.h>

    #define SLZ4_MALLOC(new_size)       malloc(new_size)
    #define SLZ4_FREE(ptr, old_size)    ((void) old_size, free(ptr))
    #define SLZ4_ASSERT(x)              assert(x)
    #define SLZ4_TEST(x)                (!(x) ? printf("SLZ4_TEST(" #x ") failed\n"), abort() : (void)0) //assert that does not get removed from release builds
    #define SLZ4_INTERNAL               inline static
    #define SLZ4_EXPORT 
#endif

//#define SLZ4_PLACE_MAGIC                 //if defined each token is prefixed with 'B' in ascii to facilitate debugging. Is not LZ4 compliant.
//#define SLZ4_TEST_AGAINST_REFERENCE_IMPL //if defined also uses the reference lz4 implementation for validation
#define SLZ4_MIN_MATCH            4
#define SLZ4_WINDOW_SIZE          0xFFFF
#define SLZ4_MAX_SIZE             0x7F000000

typedef enum SLZ4_Status {
    SLZ4_SUCCESS = 0,
    SLZ4_ERROR_OUTPUT_TOO_SMALL = -1,
    SLZ4_ERROR_INPUT_TOO_SMALL = -2,
    SLZ4_ERROR_INPUT_TOO_SMALL_LITERAL = -2,
    SLZ4_ERROR_OFFSET_ZERO = -2,
    SLZ4_ERROR_OFFSET_BIGGER_THEN_POS = -3,
    SLZ4_ERROR_INVALID_PARAMS = -4,
    SLZ4_ERROR_MALLOC_FAILED = -5,
} SLZ4_Status;

typedef struct SLZ4_Malloced {
    void* data;
    int size;
    int capacity;
    int _; //pads to mutiple of 8. unused
    SLZ4_Status status;
} SLZ4_Malloced;

typedef struct SLZ4_Compress_State {
    //between 1 and 12 determining by how many bytes to advance by before checking for a match. 
    int speed; //defaults to 1
    SLZ4_Status status;

    //If compression_table_or_null uses a default stack allocated table. 
    // With the default values for hash_size_exponent and bucket_size_exponent 
    // this uses 68KB of stack space
    void* compression_table_or_null;
    int hash_size_exponent; //defaults to 12
    int bucket_size_exponent; //defaults to 2
} SLZ4_Compress_State;

typedef struct SLZ4_Decompress_State {
    char error_message[256];
    SLZ4_Status status;
} SLZ4_Decompress_State;

//Compresses the given input into output. Returns the compressed output size or negative values from SLZ4_Status. 
// Uses the provided state_or_null or default values when null. 
// If output is NULL and output_size is 0 does a 'dry' run: goes through the entire procedure without writing anything and returns the needed *capacity* (=/= size) for the output.
SLZ4_EXPORT int slz4_compress(void* output, int output_size, const void* input, int input_size, SLZ4_Compress_State* state_or_null);

//Decompresses the given input into output. Returns the compressed output size or negative values from SLZ4_Status. 
// Uses the provided state_or_null to provide extended error information when not null.
// If output is NULL and output_size is 0 does a 'dry' run: goes through the entire procedure without writing anything and returns the needed *capacity* (= size) for the output.
SLZ4_EXPORT int slz4_decompress(void* output, int output_size, const void* input, int input_size, SLZ4_Decompress_State* state_or_null);

//Same as slz4_compress except the output is placed into returned malloced memory. Does not fail unless malloc fails.
SLZ4_EXPORT SLZ4_Malloced slz4_compress_malloc(const void* input, int input_size, SLZ4_Compress_State* state_or_null); 
//Same as slz4_decompress except the output is placed into returned malloced memory. 
SLZ4_EXPORT SLZ4_Malloced slz4_decompress_malloc(const void* input, int input_size, SLZ4_Decompress_State* state_or_null);

//Returns maximum size after compression of an input of the given size.
SLZ4_EXPORT int slz4_compressed_size_upper_bound(int size_before_compression);
//Returns the needed size in bytes for compression table (from SLZ4_Compress_State) given the provided parameters. 
SLZ4_EXPORT size_t slz4_required_size_for_compression_table(int size_exponent, int bucket_exponent);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_SLZ4_IMPL)) && !defined(JOT_SLZ4_HAS_IMPL)
#define JOT_SLZ4_HAS_IMPL

SLZ4_INTERNAL int  _slz4_find_first_set_bit64(uint64_t num);
SLZ4_INTERNAL bool _slz4_output_token(uint8_t* out, uint32_t* out_i, uint32_t output_size, uint32_t in_i, uint32_t literal_size, const uint8_t* literal_data, uint32_t match_size, uint32_t match_offset, bool is_last_literal);
SLZ4_INTERNAL uint32_t  _slz4_read_long_size(const uint8_t* in, uint32_t size, uint32_t* in_i, bool can_safely_skip_first_check);

SLZ4_EXPORT int slz4_compressed_size_upper_bound(int size_before_compression)
{
    size_t size = (size_t) size_before_compression;
    if(size > SLZ4_MAX_SIZE)
        return 1;
    size_t literal_size = size/0xFF;
    size_t max = size + literal_size + 16;
    return (int) max;
}

SLZ4_EXPORT size_t slz4_required_size_for_compression_table(int size_exponent, int bucket_exponent)
{
    SLZ4_ASSERT(0 <= size_exponent && size_exponent < 30 && 0 <= bucket_exponent && bucket_exponent <= 8);
    size_t hash_size = (size_t) 1 << size_exponent;
    size_t buckets = (size_t) 1 << bucket_exponent;
    size_t bytes = hash_size*buckets*sizeof(uint32_t) + hash_size*sizeof(uint8_t);
    return bytes;
}

SLZ4_EXPORT int slz4_compress(void* output, int output_size, const void* input, int input_size, SLZ4_Compress_State* state_or_null)
{
    if((output == NULL && output_size != 0) 
        || (input == NULL && input_size != 0) 
        || (0 > input_size || input_size > SLZ4_MAX_SIZE) 
        || (0 > output_size))
    {
        SLZ4_ASSERT(false);
        return SLZ4_ERROR_INVALID_PARAMS;
    }
    enum { END_BLOCK_RESERVED = 12 };
    
    //Compression algorithm:
    // 0. We keep a hash table of all previously seen sequences of 4 consequetive bytes. That is 
    //    a dictionary where keys are the 4 bytes and values is the absolute offset of these 4 bytes.
    // 1. We iterate byte by byte but read 4 bytes on every iteration. We hash those and look them up
    //    in the hash table. 
    // 2. If we didnt find anything go to next character. 
    // 3. If we found something (or multiple things!) see how long the match is by comparing 
    //    the byte sequences forward from current position and from the position obtained from the hash table.
    //    In case of multiple matches find the longest one.
    // 4. If outputting the maximal sized match results in net reduction of bytes output it.
    //
    // The hash table used has bucket of 2^N items in each slot. These buckets act as circular arrays so that newly
    // added items are inserted possibly on top of the last item seen. The position of the last item index within each bucket is
    // stored in a separate array of u8's. This means we dont need to worry about removing items from the hash. 
    // We also dont store the keys anywhere instead see if the memory pointed to by the match really does 
    // equal the input.

    //Use provided state or default one
    SLZ4_Compress_State default_state = {0};
    default_state.hash_size_exponent = 12;
    default_state.bucket_size_exponent = 2;
    SLZ4_Compress_State* state = state_or_null ? state_or_null : &default_state;

    //Allocate and populate the hash table
    void* hash_table_data = state->compression_table_or_null;
    if(hash_table_data == NULL)
    {   
        size_t needed_size = slz4_required_size_for_compression_table(state->hash_size_exponent, state->bucket_size_exponent);
        SLZ4_ASSERT(needed_size < 10*1024*1024 && "you probably dont want to allocate 10MB of stack space!");
        #if defined(_MSC_VER)
            hash_table_data = _alloca(needed_size);
        #elif defined(__GNUC__) || defined(__clang__)
            hash_table_data = __builtin_alloca(needed_size);
        #else
            #error unsupported compiler!
        #endif
    }
    
    uint32_t hash_exponent = (uint32_t) state->hash_size_exponent;
    uint32_t bucket_size = (uint32_t) (1 << state->bucket_size_exponent);
    uint32_t hash_size = 1 << hash_exponent;
    uint32_t bucket_size_mask = bucket_size - 1;
    uint32_t speed = state->speed;
    if(speed < 1) speed = 1;
    if(speed > END_BLOCK_RESERVED) speed = END_BLOCK_RESERVED;
    
    uint32_t* hash = (uint32_t*) hash_table_data;
    uint8_t* buckets_last = (uint8_t*) (void*) (hash + hash_size*bucket_size);

    memset(hash,     0xFF,    hash_size*bucket_size*sizeof(uint32_t));
    memset(buckets_last, 0, hash_size*sizeof(uint8_t));
    
    //Used variables
    const uint8_t* in = (const uint8_t*) input;
    uint8_t* out = (uint8_t*) output;

    uint32_t in_i = 0;
    uint32_t out_i = 0;
    uint32_t out_size = (uint32_t) output_size;
    uint32_t in_size = (uint32_t) input_size;
    uint32_t last_token_in_i = 0;

    //By pretend the input is 12B smaller we: 
    // 1. are compliant with the standard
    // 2. allow to overread full 8B quadword which allows us to use 64bit 
    //    numbers for string comparisons. 
    bool okay = true;
    uint32_t token_count = 0;
    if(input_size > END_BLOCK_RESERVED)
    {
        in_size = (uint32_t) input_size - END_BLOCK_RESERVED;
        for(; in_i < in_size; token_count++)
        {
            #define slz4_hash(val) (((val) * 2654435761U) >> (32-hash_exponent))

            //Read 8 bytes. Calculate the hash using the first 4 and lookup the appropriate bucket.
            uint64_t first_input_read = 0; memcpy(&first_input_read, in + in_i, sizeof first_input_read);
            uint32_t first_hash_index = slz4_hash((uint32_t) first_input_read);

            SLZ4_ASSERT(first_hash_index < hash_size);
            uint32_t* bucket_offsets = &hash[first_hash_index*bucket_size];
            uint8_t* bucket_last = &buckets_last[first_hash_index];

            //find longest match in the bucket. 
            //Starts from the most recently added and stops when either:
            // 1. Iterated the whole bucket
            // 2. The position pointed to by the slot is outside of the window
            uint32_t longest_match_pos = 0;
            uint32_t longest_match_size = 0;
            for(uint32_t k = 0; k < bucket_size; k++)
            {
                uint32_t match_bucket_i = (*bucket_last - k - 1) & bucket_size_mask;
                uint32_t match_pos = bucket_offsets[match_bucket_i];

                if(match_pos + SLZ4_WINDOW_SIZE < in_i || match_pos > in_i)
                    break;
                
                //Get the length of the match. Compare as the matched data with the current input data
                // using 64 bit numbers. When these numbers differ get the position of the difference
                // using _slz4_find_first_set_bit64.
                SLZ4_ASSERT(match_pos < in_size);
                uint32_t match_size = 0;
                for(; in_i + match_size <= in_size; match_size += 8)
                {
                    uint64_t input_read = 0; memcpy(&input_read, in + in_i + match_size, sizeof input_read);
                    uint64_t match_read = 0; memcpy(&match_read, in + match_pos + match_size, sizeof match_read);
                    uint64_t comp = input_read ^ match_read;
                    if(comp != 0)
                    {
                        int diff_bit_i = _slz4_find_first_set_bit64(comp);
                        match_size += diff_bit_i/8;
                        break;
                    }
                }

                if(longest_match_size < match_size)
                {
                    longest_match_size = match_size;
                    longest_match_pos = match_pos;
                }
            }
            
            //Insert the current byte sequence (first_input_read) into the hash
            bucket_offsets[*bucket_last] = in_i;
            *bucket_last = (*bucket_last + 1) & (bucket_size - 1);
            
            //Decide whether to add the found longest match as a math or insert a literal
           
            //When we have a super long literal the size specifier 
            // is composed of many 0xFF (= 0xFF) bytes right next to each other.
            // this size is non negligable and we dont want to have to specify it
            // again just because we found SLZ4_MIN_MATCH random matching bytes.
            uint32_t literal_size = in_i - last_token_in_i;
            uint32_t literal_size_cost = literal_size/0xFF + 1;
            
            if(in_i + longest_match_size > in_size)
                longest_match_size = in_size - in_i;

            if(longest_match_size <= literal_size_cost || longest_match_size < SLZ4_MIN_MATCH)
                in_i += speed;
            else
            {
                SLZ4_ASSERT(longest_match_size >= SLZ4_MIN_MATCH);
                SLZ4_ASSERT(in_i >= last_token_in_i);
                SLZ4_ASSERT(in_i >= longest_match_pos);

                uint32_t match_offset = in_i - longest_match_pos;
                if(_slz4_output_token(out, &out_i, out_size, in_i, literal_size, in + last_token_in_i, longest_match_size, match_offset, false) == false)
                {
                    okay = false;
                    break;
                }

                //Add all bytes skipped over as result of using the match to the hash. 
                // We know longest_match_size is at least SLZ4_MIN_MATCH (= 4) thus we 
                // can unroll the begginign of the loop
                SLZ4_ASSERT(in_i + longest_match_size <= in_size);
                for(uint32_t k = 1; k < longest_match_size; k += speed)
                {
                    uint32_t curr_read = 0; memcpy(&curr_read, in + in_i + k, sizeof curr_read);
                    uint32_t curr_hash = slz4_hash(curr_read);
                    uint32_t curr_offset = in_i + k;

                    uint32_t* curr_bucket_offsets = &hash[curr_hash*bucket_size];
                    uint8_t* curr_bucket_last = &buckets_last[curr_hash];

                    curr_bucket_offsets[*curr_bucket_last] = curr_offset;
                    *curr_bucket_last = (*curr_bucket_last + 1) & (bucket_size - 1);
                }

                in_i += longest_match_size;
                last_token_in_i = in_i;
            }
            SLZ4_ASSERT(in_i <= in_size + speed);
        }
        #undef slz4_hash
    }
    
    //Add a last token literal of the size not used. We do not attempt to perform any compression here.
    uint32_t remaining = (uint32_t) input_size - last_token_in_i;
    okay = okay && _slz4_output_token(out, &out_i, out_size, in_i, remaining, (const uint8_t*) input + last_token_in_i, 0, 0, true);

    //When in 'dry' run mode add some extra bytes so that the real decompression works
    // (the decompression needs a few extra bytes to make checks faster see _slz4_output_token)
    if(out == NULL)
        out_i += 16;

    return okay ? out_i : SLZ4_ERROR_OUTPUT_TOO_SMALL;
}

SLZ4_INTERNAL bool _slz4_output_token(uint8_t* out, uint32_t* out_i, uint32_t output_size, uint32_t in_i, uint32_t literal_size, const uint8_t* literal_data, uint32_t match_size, uint32_t match_offset, bool is_last_literal)
{
    //Check if we have enough space in output. This is an upper bound check, 
    // thus sometimes this can fire even when the output is sufficiently sized for 
    // the compressed output. However this occurs only for difference of few bytes.
    uint32_t max_token_size = 1 //token
        + literal_size/0xFF + 2 //literal size
        + literal_size          //literal data
        + match_size/0xFF + 2   //match size
        + 2                     //match offset
        + 1;                    //magic (if enabled)

    uint32_t needed_size = max_token_size + *out_i;
    if(needed_size > output_size && out != NULL)
        return false;
    
    //Because we support 'dry' runs we kinda have to do this ugly hack
    // I wish I knew of a solution that isnt so ugly but whatever 
    #define push_out(val) out ? out[(*out_i)++] = val : (*out_i)++
    //#define push_out(val) out[(*out_i)++] = val

    #ifdef SLZ4_PLACE_MAGIC
        push_out('B');
    #endif 

    //Output literal part of the token
    uint8_t* token = &out[(*out_i)++];
    uint32_t tok_literals = literal_size;
    if(literal_size > 0)
    {
        if(literal_size >= 0xF)
        {   
            tok_literals = 0xF;
            uint32_t curr_literal_size = literal_size - 0xF;
            for(; curr_literal_size >= 0xFF; curr_literal_size -= 0xFF)
                push_out(0xFF);

            push_out((uint8_t) curr_literal_size);
        }
        
        if(out) memcpy(out + *out_i, literal_data, literal_size);
        *out_i += literal_size;
    }
    
    //Output match part of the token
    uint32_t tok_match = 0;;
    if(is_last_literal == false)
    {
        SLZ4_ASSERT(match_size >= SLZ4_MIN_MATCH);
        SLZ4_ASSERT(0 < match_offset && match_offset <= UINT16_MAX && match_offset <= in_i);

        if(out) memcpy(out + *out_i, &match_offset, sizeof(uint16_t));
        *out_i += 2;

        uint32_t curr_match_size = match_size - SLZ4_MIN_MATCH;
        if(curr_match_size >= 0xF)
        {
            tok_match = 0xF;
            curr_match_size -= 0xF;
            for(; curr_match_size >= 0xFF; curr_match_size -= 0xFF)
                push_out(0xFF);

            push_out((uint8_t) curr_match_size);
        }
        else
            tok_match = curr_match_size;
    }

    #undef push_out

    //Fill in the token data
    SLZ4_ASSERT(tok_literals <= 0xF);
    SLZ4_ASSERT(tok_match <= 0xF);
    if(out) *token = (uint8_t) (tok_literals << 4 | tok_match);
    
    SLZ4_ASSERT(*out_i <= needed_size && "our prediction of maximum token size must be valid!");
    return true;
}

SLZ4_EXPORT int slz4_decompress(void* output, int output_size, const void* input, int input_size, SLZ4_Decompress_State* state_or_null)
{
    const uint8_t* in = (const uint8_t*) input;
    uint8_t* out = (uint8_t*) output;

    uint32_t in_i = 0;
    uint32_t out_i = 0;
    
    uint32_t in_size = 0;
    uint32_t out_size = 0;

    uint32_t last_token_in_i = 0;
    uint32_t last_token_out_i = 0;
    
    uint8_t token = 0; 
    uint32_t literals_size = 0;
    uint32_t match_offset = 0;
    uint32_t match_size = 0;
    
    int return_value = 0;
    
    if((input == NULL && input_size != 0) 
        || (0 > input_size || input_size > SLZ4_MAX_SIZE))
        goto error_invalid_params_in;
        
    if((output == NULL && output_size != 0)
        || (0 > output_size || output_size > SLZ4_MAX_SIZE))
        goto error_invalid_params_out;

    //"dry" run to get size only. Assume output is as big as it needs to be
    if(output == NULL && output_size == 0)
        output_size = SLZ4_MAX_SIZE;

    //We drastically speed up the decoding by not performing any bounds checks for
    // all things that have bounded offset/lenght. We simply again pretend the output_size
    // is smaller by the upper bound of the static offset/length limit thus adding 'padding'.
    // It just happens that everything on the hot path has bounded size so we only do about
    // 1 check per token.
    //We make heavy use of the fact we can always write more then required. This is not a problem
    // because of the padding and the fact we will simply overwrtie this superficial data with 
    // something else later.
    //I have marked the hot path by comments.
    enum {FAST_PHASE_PADDING = 2*32};
    if(input_size > FAST_PHASE_PADDING && output_size > FAST_PHASE_PADDING)
    {
        in_size = (uint32_t) input_size - FAST_PHASE_PADDING;
        out_size = (uint32_t) output_size - FAST_PHASE_PADDING;

        for(;;)
        {
            last_token_in_i = in_i;
            last_token_out_i = out_i;

            #ifdef SLZ4_PLACE_MAGIC
                uint8_t magic = in[in_i++];
                SLZ4_ASSERT(magic == 'B');
            #endif // DEBUG
            
            //*hot path*
            //The only size check on the hot path!
            if(in_i >= in_size)  
                break;
                
            token = in[in_i++]; 
            literals_size = token >> 4;
            match_size = token & 0xF;

            if(literals_size == 0xF)
            {
                literals_size = _slz4_read_long_size(in, in_size, &in_i, true);
                if(literals_size == 0 || in_i + literals_size > in_size || out_i + literals_size > out_size)  
                    break;

                if(out)
                    for(uint32_t i = 0; i < literals_size; i += 32)
                        memcpy(out + out_i + i, in + in_i + i, 32);
            }
            else
            {
                //*hot path*
                //We need to copy less then 15 but we always copy 16
                // because that is just one 16B load/store instruction pair
                //No need to check size because the required size is bounded.
                if(out)
                    memcpy(out + out_i, in + in_i, 16);
            }

            
            out_i += literals_size;
            in_i += literals_size;

            //Read in the offset. No need to check size because the 2 require bytes are bounded.
            memcpy(&match_offset, in + in_i, sizeof(uint16_t)); in_i += 2;

            if(match_size == 0xF)
            {
                match_size = _slz4_read_long_size(in, in_size, &in_i, true);
                if(match_size == 0)
                    break;
            }
            else
            {
                //*hot path*
                //We need match_offset >= 8 so that we can efficiently copy
                //We need out_i >= match_offset to ensure we are not reading before
                // the start of the output array. We know match_offset is uint16_t thus 
                // when out_i >= SLZ4_WINDOW_SIZE we will also satisfy out_i >= match_offset
                // and it happens to make the check A LOT cheaper (because of superscalar)
                if(match_offset >= 8 && out_i >= SLZ4_WINDOW_SIZE)
                {
                    if(out)
                    {
                        //Again no need to check sizes because its bounded
                        memcpy(out + out_i, out + out_i - match_offset, 8);
                        memcpy(out + out_i + 8, out + out_i - match_offset + 8, 8);
                        memcpy(out + out_i + 16, out + out_i - match_offset + 16, 4);
                    }

                    //Skip expensive the check below (*) continuing on the hot path to the next
                    // token
                    out_i += match_size + 4;
                    continue;
                }
            }
        
            //Match size is possibly unbounded so we need to check
            match_size += 4;
            if(match_offset > out_i || match_offset == 0 || out_i + match_size > out_size) // (*)
                break;

            if(out)
            if(match_size <= match_offset)
            {
                for(uint32_t i = 0; i < match_size; i += 32)
                {
                    //We the source and destination for the copy operation *can* overlap
                    // as such we need to use "temporary buffer". This does not make any difference
                    // however, because the data will be coppied to registers and then back from registers
                    // to memory regrdless. This is just not to trigger UB.
                    uint64_t buff[4] = {0};
                    memcpy(buff, out + out_i - match_offset + i, 32);
                    memcpy(out + out_i + i, buff, 32);
                }
            }
            else
            {
                for(uint32_t i = 0; i < match_size; i++)
                    out[out_i + i] = out[out_i - match_offset+i];
            }

            out_i += match_size;
        }
    }
    
    //Start on the last token from the fast phase
    in_i = last_token_in_i;
    out_i = last_token_out_i;
    in_size = (uint32_t) input_size;
    out_size = (uint32_t) output_size;
    
    //The "careful" loop handling the last ~64B. 
    // This is the vanilla implementation. Because its performance 
    // does not really matter some obvious performance improvements are not implemented. 
    // In particular we perform the copying from match byte by byte always.
    // Feel free to copy paste just this loop into your own code if you need a minimal implementation.
    for(;;)
    {
        last_token_in_i = in_i;
        last_token_out_i = out_i;

        if(in_i >= in_size)  
            goto error_input_out_of_bounds;

        #ifdef SLZ4_PLACE_MAGIC
            uint8_t magic = in[in_i++];
            SLZ4_ASSERT(magic == 'B');
        #endif // DEBUG

        token = in[in_i++]; 
        literals_size = token >> 4;
        match_size = token & 0xF;
        
        if(literals_size == 0xF)
        {
            literals_size = _slz4_read_long_size(in, in_size, &in_i, false);
            if(literals_size == 0)
                goto error_input_out_of_bounds;
        }
        
        if(in_i + literals_size > in_size || out_i + literals_size > out_size)  
            goto error_literal_out_bounds;

        if(out)
            memcpy(out + out_i, in + in_i, literals_size);
        out_i += literals_size;
        in_i += literals_size;

        if(in_i == in_size)
            break;
        
        if(in_i + 2 > in_size)  
            goto error_input_out_of_bounds;

        memcpy(&match_offset, in + in_i, sizeof(uint16_t)); in_i += 2;

        if(match_size == 0xF)
        {
            match_size = _slz4_read_long_size(in, in_size, &in_i, false);
            if(match_size == 0)
                goto error_input_out_of_bounds;
        }

        match_size += 4;
        if(match_offset > out_i || match_offset == 0 || out_i + match_size > out_size)
            goto error_invalid_offset_or_match;
        
        if(out)
        {
            for(uint32_t i = 0; i < match_size; i++)
                out[out_i + i] = out[out_i - match_offset+i];
        }

        out_i += match_size;
    }

    //Report errors
    return_value = (int) out_i;
    while(0) 
    {
        error_invalid_params_in: {
            SLZ4_ASSERT(false);
            return_value = SLZ4_ERROR_INVALID_PARAMS;
            if(state_or_null)  snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                "Invalid input params provided. "
                "input=0x%08llx input_size=%i output=0x%08llx output_size=%i", (long long) input, input_size, (long long) output, output_size);
            break;
        }
        error_invalid_params_out: {
            SLZ4_ASSERT(false);
            return_value = SLZ4_ERROR_INVALID_PARAMS;
            if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                "Invalid output params provided. "
                "input=0x%08llx input_size=%i output=0x%08llx output_size=%i", (long long) input, input_size, (long long) output, output_size);
            break;
        }
        error_input_out_of_bounds: {
            return_value = SLZ4_ERROR_INPUT_TOO_SMALL;
            if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                "Input out of bounds while reading generic value (not literal or match data). "
                "input_size=%i output_size=%i last_token_in_i=%u", input_size, output_size, last_token_in_i);
            break;
        }
        error_literal_out_bounds: {
            if(in_i + literals_size > in_size)  
            {
                return_value = SLZ4_ERROR_INPUT_TOO_SMALL;
                if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                    "Input out of bounds while reading literal of size %u. "
                    "input_size=%i output_size=%i last_token_in_i=%u", literals_size, input_size, output_size, last_token_in_i);
            }
            if(out_i + literals_size > out_size)  
            {
                return_value = SLZ4_ERROR_OUTPUT_TOO_SMALL;
                if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                    "Output out of bounds while reading literal of size %u on output position %u. "
                    "input_size=%i output_size=%i last_token_in_i=%u", literals_size, out_i, input_size, output_size, last_token_in_i);
            }
            break;
        }
        error_invalid_offset_or_match: {
            if(match_offset == 0)
            {
                return_value = SLZ4_ERROR_OFFSET_ZERO;
                if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                    "Corrupted token with offset 0. "
                    "input_size=%i output_size=%i last_token_in_i=%u", input_size, output_size, last_token_in_i);
            }
            if(match_offset > out_i)
            {
                return_value = SLZ4_ERROR_OFFSET_BIGGER_THEN_POS;
                if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                    "Token references data before start of the output buffer. match_offset=%u out_i=%u."
                    "input_size=%i output_size=%i last_token_in_i=%u", match_offset, out_i, input_size, output_size, last_token_in_i);
            }
            if(out_i + match_size > out_size)
            {
                return_value = SLZ4_ERROR_OUTPUT_TOO_SMALL;
                if(state_or_null) snprintf(state_or_null->error_message, sizeof state_or_null->error_message, 
                    "Output out of bounds while reading match of size %u on output position %u. "
                    "input_size=%i output_size=%i last_token_in_i=%u", match_size, out_i, input_size, output_size, last_token_in_i);
            }
            break;
        }
    }

    if(state_or_null)
        state_or_null->status = return_value < 0 ? return_value : 0;
    
    return return_value;
}

SLZ4_INTERNAL uint32_t _slz4_read_long_size(const uint8_t* in, uint32_t size, uint32_t* in_i, bool can_safely_skip_first_check)
{
    //0 signals error!

    //When padding is used we have performed size check at the start of the loop
    // thus we can safely skip the first check. But ONLY the first one since there 
    // can be unbound number of literals overall...
    if(can_safely_skip_first_check == false && *in_i >= size)  
        return 0; 

    uint32_t literals_size = in[*in_i];
    while(in[(*in_i)++] == 0xFF)
    {
        if(*in_i >= size)
            return 0;

        literals_size += in[*in_i];
    }

    return literals_size + 0xF;
}

#if defined(_MSC_VER)
    #include <intrin.h>
    SLZ4_INTERNAL int32_t _slz4_find_first_set_bit64(uint64_t num)
    {
        SLZ4_ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
#elif defined(__GNUC__) || defined(__clang__)
    SLZ4_INTERNAL int32_t _slz4_find_first_set_bit64(uint64_t num)
    {
        SLZ4_ASSERT(num != 0);
        return __builtin_ffsll((long long) num) - 1;
    }
#else
    #error unsupported compiler!
#endif

SLZ4_EXPORT SLZ4_Malloced slz4_compress_malloc(const void* input, int input_size, SLZ4_Compress_State* state_or_null)
{    
    SLZ4_Malloced malloced = {0};
    int capacity = slz4_compressed_size_upper_bound(input_size);
    void* data = SLZ4_MALLOC((size_t) capacity);
    if(data == NULL)
    {
        malloced.status = SLZ4_ERROR_MALLOC_FAILED;
        return malloced;
    }

    int size = slz4_compress(data, capacity, input, input_size, state_or_null);
    if(size < 0)
    {
        SLZ4_FREE(data, capacity);
        malloced.status = (SLZ4_Status) size;
        return malloced;
    }

    malloced.data = data;
    malloced.size = size;
    malloced.capacity = capacity;
    return malloced;
}

SLZ4_EXPORT SLZ4_Malloced slz4_decompress_malloc(const void* input, int input_size, SLZ4_Decompress_State* state_or_null)
{
    SLZ4_Malloced malloced = {0};
    int size = slz4_decompress(NULL, 0, input, input_size, state_or_null);
    if(size <= 0)
    {
        malloced.status = (SLZ4_Status) size;
        return malloced;
    }

    malloced.data = SLZ4_MALLOC((size_t) size);
    if(malloced.data == NULL)
    {
        malloced.status = SLZ4_ERROR_MALLOC_FAILED;
        return malloced;
    }

    malloced.size = size;
    malloced.capacity = size;
    int new_status = slz4_decompress(malloced.data, malloced.size, input, input_size, state_or_null);
    SLZ4_ASSERT(new_status == size);
    return malloced;
}

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_SLZ4_TEST)) && !defined(JOT_SLZ4_HAS_TEST)
#define JOT_SLZ4_HAS_TEST

#include <time.h>
SLZ4_EXPORT void slz4_test_unit();
SLZ4_EXPORT void slz4_test_sizes(double seconds);
SLZ4_EXPORT void slz4_test_invalid_decompress(double seconds);

SLZ4_INTERNAL void _slz4_test_get_rotated_text(char* string, int size);
SLZ4_INTERNAL double _slz4_now();

SLZ4_EXPORT void slz4_test(double seconds)
{
    slz4_test_unit();
    slz4_test_sizes(seconds/2);
    slz4_test_invalid_decompress(seconds/2);
}

SLZ4_EXPORT void slz4_test_roundtrip(const void* data, int size)
{
    //Compress and decompress and test whether we got back the same thing
    int compressed_capacity = slz4_compress(NULL, 0, data, size, NULL);
    SLZ4_TEST(compressed_capacity >= 0);
    
    char* compressed = (char*) calloc(compressed_capacity, 1);
    SLZ4_TEST(compressed != NULL);
    int compressed_size = slz4_compress(compressed, compressed_capacity, data, size, NULL);
    SLZ4_TEST(compressed_size > 0);
    
    int decompressed_capacity = slz4_decompress(NULL, 0, compressed, compressed_size, NULL);
    SLZ4_TEST(decompressed_capacity == size);
    
    char* decompressed = (char*) calloc(decompressed_capacity, 1);
    SLZ4_TEST(decompressed != NULL);
    int decompressed_size = slz4_decompress(decompressed, decompressed_capacity, compressed, compressed_size, NULL);
    SLZ4_TEST(decompressed_size >= 0);
    SLZ4_TEST(memcmp(data, decompressed, size) == 0);

    //Test malloc interface. It should give the exact same results.
    SLZ4_Malloced compressed_malloc = slz4_compress_malloc(data, size, NULL);
    SLZ4_Malloced decompressed_malloc = slz4_decompress_malloc(compressed_malloc.data, compressed_malloc.size, NULL);
    SLZ4_TEST(compressed_malloc.size == compressed_size);
    SLZ4_TEST(decompressed_malloc.size == decompressed_size);
    SLZ4_TEST(memcmp(compressed_malloc.data, compressed, compressed_size) == 0);
    SLZ4_TEST(memcmp(decompressed_malloc.data, decompressed, decompressed_size) == 0);

    //printf("Compressed %i -> %i Bytes ~%.2lf \n", size, compressed_size, (double) size / (double) compressed_size);
    
    //Test against the reference implementation. 
    //It should be able to decompress our compression and we should be able to decompress its compression.
    #ifdef SLZ4_TEST_AGAINST_REFERENCE_IMPL
        int ref_decompressed_size = LZ4_decompress_safe(compressed, decompressed, compressed_size, decompressed_capacity);
        SLZ4_TEST(ref_decompressed_size == size);
        SLZ4_TEST(memcmp(data, decompressed, size) == 0);

        int ref_compressed_capacity = LZ4_compressBound(size);
        char* ref_compressed = (char*) malloc((size_t) ref_compressed_capacity);
        SLZ4_TEST(ref_compressed != NULL);

        int ref_compressed_size = LZ4_compress_default(data, ref_compressed, size, ref_compressed_capacity);
        SLZ4_TEST(ref_compressed_size > 0);
    
        decompressed_size = slz4_decompress(decompressed, decompressed_capacity, ref_compressed, ref_compressed_size, NULL);
        SLZ4_TEST(decompressed_size == size);
        SLZ4_TEST(memcmp(data, decompressed, size) == 0);

        free(ref_compressed);
    #endif

    free(compressed);
    free(decompressed);
    SLZ4_FREE(compressed_malloc.data, compressed_malloc.capacity);
    SLZ4_FREE(decompressed_malloc.data, decompressed_malloc.capacity);
}

SLZ4_EXPORT void slz4_test_roundtrip_string(const char* data)
{
    slz4_test_roundtrip(data, (int) strlen(data));
}

SLZ4_EXPORT void slz4_test_unit()
{
    enum {MAX_TEST_SIZE = 1 << 24};
    char* testing_buffer = (char*) malloc(MAX_TEST_SIZE);

    printf("sLZ4 Testing on few specific strings\n");
    slz4_test_roundtrip_string("");
    slz4_test_roundtrip_string("a");
    slz4_test_roundtrip_string("aa");
    slz4_test_roundtrip_string("aaa");
    slz4_test_roundtrip_string("aaaaa");
    slz4_test_roundtrip_string("aaaaaaaa");
    slz4_test_roundtrip_string("aaaaaaaaaaaaaaaa");
    slz4_test_roundtrip_string("aaaaaaaaaaaaaaaaaaaaaaaaaa");
    slz4_test_roundtrip_string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    slz4_test_roundtrip_string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    slz4_test_roundtrip_string("Hello world! xxx Hello world! yyy Hello world! zzz Hello world! xxx Hello world! xxx ");
    slz4_test_roundtrip_string("abcdefghijklmnopqrstuvwxyz0123456789_ABCDEFGHIJKLMNOPQRTSUVWXYZ");
    slz4_test_roundtrip_string("abcdefghijklmnopqrstuvwxyz0123456789_____________________abcdefghijklmnopqrstuvwxyz0123456789_________");
    slz4_test_roundtrip_string(
        "_____________________________________________________________________________________________________"
        "_____________________________________________________________________________________________________"
        "_____________________________________________________________________________________________________"
        "_____________________________________________________________________________________________________"
        "_____________________________________________________________________________________________________"
        "__________________________________________________________________________________________________XXX"
        );
    slz4_test_roundtrip_string(
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore e"
        "t dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut ali"
        "quip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum d"
        "olore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui off"
        "icia deserunt mollit anim id est laborum");
    slz4_test_roundtrip_string(
        "p=function(t,e,s,i){var a,r,n,h,o,l,p=P,f=0,d=[],m=[],c=Ft.newElement();for(n=s.length,a=0;a<p;a+=1)"
        "{for(o=a/(p-1),r=l=0;r<n;r+=1)h=_(1-o,3)*t[r]+3*_(1-o,2)*o*s[r]+3*(1-o)*_(o,2)*i[r]+_(o,3)*e[r],d[r]"
        "=h,null!==m[r]&&(l+=_(d[r]-m[r],2)),m[r]=d[r];l&&(f+=l=k(l)),c.percents[a]=o,c.lengths[a]=f}return c"
        ".addedLength=f,c};function y(t){this.segmentLength=0,this");
        
    printf("sLZ4 Testing on big semi-random latin text of size %i\n", MAX_TEST_SIZE);
    _slz4_test_get_rotated_text(testing_buffer, 4096);
    slz4_test_roundtrip_string(testing_buffer);
    
    _slz4_test_get_rotated_text(testing_buffer, 4096*4);
    slz4_test_roundtrip_string(testing_buffer);
    
    _slz4_test_get_rotated_text(testing_buffer, MAX_TEST_SIZE);
    slz4_test_roundtrip_string(testing_buffer);

    free(testing_buffer);
}

SLZ4_EXPORT void slz4_test_sizes(double seconds)
{
    enum {
        MAX_TEST_SIZE = 1 << 24, //16MB
        DETAILED_TESTING = 256,
    };
    char* testing_buffer = (char*) malloc(MAX_TEST_SIZE);
    double start = _slz4_now();

    //Runs are perfectly compressible. The result should be just two tokens. 
    //One for the run the seconds for the last few bytes
    printf("sLZ4 Testing on runs of all sizes <= %i\n", DETAILED_TESTING);
    for(uint32_t size = 0; size < DETAILED_TESTING; size ++)
    {
        memset(testing_buffer, 'x', size);
        slz4_test_roundtrip(testing_buffer, size);
    }
    
    printf("sLZ4 Testing on runs of all power of two sizes of <= %i\n", MAX_TEST_SIZE);
    for(uint32_t size = DETAILED_TESTING; size <= MAX_TEST_SIZE; size *= 2)
    {
        memset(testing_buffer, 'x', size);
        slz4_test_roundtrip(testing_buffer, size);
    }
    
    //Courrpted text is compressible by about 33%. 
    for(int i = 0; _slz4_now() < start + seconds/2; i ++)
    {
        _slz4_test_get_rotated_text(testing_buffer, MAX_TEST_SIZE);

        printf("sLZ4 Testing on data of all sizes <= %i #%i\n", DETAILED_TESTING, i+1);
        for(uint32_t size = 0; size < DETAILED_TESTING; size ++)
            slz4_test_roundtrip(testing_buffer, size);
            
        printf("sLZ4 Testing on data of all power of two sizes <= %i #%i\n", MAX_TEST_SIZE, i+1);
        for(uint32_t size = DETAILED_TESTING; size < MAX_TEST_SIZE; size *= 2)
            slz4_test_roundtrip(testing_buffer, size);
    }

    //Random data is typically uncompressible
    srand(clock());
    for(int i = 0; _slz4_now() < start + seconds/2; i ++)
    {
        for(int j = 0; j < MAX_TEST_SIZE; j++)
            testing_buffer[j] = (char) (rand() % 256);

        printf("sLZ4 Testing on random data of all sizes <= %i #%i\n", DETAILED_TESTING, i+1);
        for(uint32_t size = 0; size < DETAILED_TESTING; size ++)
            slz4_test_roundtrip(testing_buffer, size);

        printf("sLZ4 Testing on random data of all power of two sizes <= %i #%i\n", MAX_TEST_SIZE, i+1);
        for(uint32_t size = DETAILED_TESTING; size < MAX_TEST_SIZE; size *= 2)
            slz4_test_roundtrip(testing_buffer, size);
    }

    free(testing_buffer);
}

SLZ4_EXPORT void slz4_test_invalid_decompress(double seconds)
{
    enum {
        MAX_TEST_SIZE = 1 << 24, //16MB
        ONE_OVER_CORRUPTION_CHANCE = 512, 
        CORRUPT_MAX_VALUE = 2,
        CORRUPT_MIN_VALUE = -2,
        TARGET_SIZE = 4096*4,
    };

    printf("sLZ4 Testing on corrupted input \n");
    
    double start = _slz4_now();
    char* testing_buffer = (char*) malloc(MAX_TEST_SIZE);
    _slz4_test_get_rotated_text(testing_buffer, MAX_TEST_SIZE);
    
    //We test on invalid input. 
    //We generate a compressed output of exactly 4*4096 B (by binary search)
    // allocate that on a lone page and then randomly slightly modify bytes.
    // We should never touch outside of the page and generate errors. 
    // Most of the errors are due to attempt to read before the start of the input because
    // of the fact that the match offset is encoded in two bytes thus if we corrupt the high byte 
    // the match offset is changed by corruption*256.
    // We use malloc because we dont want to pull entire windows.h and hope that will work sanely.
    // If you want more safety you can run it with address sanitizer.

    void* compressed = malloc(TARGET_SIZE);
    int decompressed_size = MAX_TEST_SIZE;
    int upper = MAX_TEST_SIZE;
    int lower = 0;
    bool found = false;
    while(!found)
    {
        SLZ4_TEST(decompressed_size <= MAX_TEST_SIZE);

        decompressed_size = (lower + upper)/2;
        SLZ4_Malloced mid = slz4_compress_malloc(testing_buffer, decompressed_size, NULL);
        if(mid.size == TARGET_SIZE)
        {
            found = true;
            memcpy(compressed, mid.data, TARGET_SIZE);
        }

        if(mid.size > TARGET_SIZE)
            upper = decompressed_size - 1;
        if(mid.size < TARGET_SIZE)
            lower = decompressed_size + 1;
            
        SLZ4_FREE(mid.data, mid.capacity);
    }
    
    uint8_t* corrupted = (uint8_t*) malloc(TARGET_SIZE);
    void* decode_into = malloc(decompressed_size);
    
    srand(clock());
    for(int i = 0; i == 0 || _slz4_now() < start + seconds; i ++)
    {
        memcpy(corrupted, compressed, TARGET_SIZE);
        uint32_t corrupt_count = TARGET_SIZE/ONE_OVER_CORRUPTION_CHANCE;
        for(uint32_t k = 0; k < corrupt_count; k++)
        {
            int index = rand() % TARGET_SIZE;
            int corrupt_by = rand() % (CORRUPT_MAX_VALUE - CORRUPT_MIN_VALUE) + CORRUPT_MIN_VALUE;
            corrupted[index] = (uint8_t) (corrupted[index] + corrupt_by);
        }

        SLZ4_Decompress_State state = {0};
        int size = slz4_decompress(decode_into, decompressed_size, corrupted, TARGET_SIZE, &state);
        if(size < 0)
        {
            SLZ4_TEST(state.status == size);
            SLZ4_TEST(strlen(state.error_message) != 0);
            //printf("%s\n", state.error_message);
        }
    }
    
    free(testing_buffer);
    free(compressed);
    free(corrupted);
    free(decode_into);
}

SLZ4_INTERNAL double _slz4_now()
{
    static bool first_time_init = false;
    static clock_t first_time = 0;
    if(first_time_init == false)
    {
        first_time = clock();
        first_time_init = true;
    }

    double now = (double) (clock() - first_time) / CLOCKS_PER_SEC;
    return now;
}

SLZ4_INTERNAL void _slz4_test_get_rotated_text(char* into, int size)
{
    #define ID(x) x
    #define STRINGIFY(...) ID(#__VA_ARGS__)

    //believe it or not but there is actually a limit to how long C strings can be... 
    //Its 509B by standard but compilers allow more. This blob is 4096 bytes. 
    //Should be reasonably portable an if not just split it into multiple and concatenate in code.
    const char* long_string_base = STRINGIFY(
        Mauris ornare lacus eu consequat elementum. Pellentesque maximus bibendum
        nulla sed porta. Maecenas ex ipsum, luctus eu sem sed, congue blandit ante. In hac
        habitasse platea dictumst. Nam hendrerit at tellus eu tincidunt. Praesent porttitor ex
        at libero vestibulum, vel eleifend quam maximus. Aenean ligula massa, porttitor
        vel imperdiet vel, cursus ut nibh. Nullam consectetur vestibulum condimentum.
        Integer venenatis lorem posuere elit efficitur pharetra. Nunc et nisl eu magna
        venenatis tincidunt. Nam hendrerit a enim sed vehicula. Vivamus massa sapien, interdum
        non metus a, pellentesque molestie est. Sed imperdiet ex non aliquam mollis.  Sed
        maximus a dui finibus aliquam. Aenean laoreet mi tellus, sed euismod dui cursus
        euismod. Quisque ultrices lobortis accumsan. Praesent efficitur felis ex, vel dapibus
        turpis rutrum at. Proin gravida, metus in aliquam interdum, est nibh sodales nibh, in
        rhoncus neque ante sed nibh. Proin non nibh non lectus euismod tincidunt. Donec in
        malesuada lacus. Donec pharetra ante enim. Vestibulum vehicula elit posuere nisi
        iaculis, nec dapibus turpis iaculis. Ut scelerisque ac leo nec aliquam. Aliquam sed
        tellus at quam suscipit lobortis. Fusce et mauris ut quam faucibus varius eu eu
        tellus. Donec viverra metus luctus leo elementum, ut porttitor erat efficitur.
        Curabitur luctus convallis scelerisque. Suspendisse volutpat neque posuere, suscipit
        lectus a, venenatis lacus. Quisque non ex nisl.  Aenean lobortis lorem eu tellus
        malesuada, a malesuada odio egestas. Proin non dignissim elit. Suspendisse at fermentum
        tellus. Ut a feugiat lectus. Ut nec est vel augue pharetra consequat. Morbi sed
        ultricies elit. Donec tortor massa, volutpat sit amet elementum ut, fringilla quis
        neque. Fusce facilisis a metus non volutpat. Sed purus purus, feugiat sit amet sapien
        a, pulvinar gravida velit. Morbi ultricies tellus eget nisl porttitor pharetra.
        Nam efficitur orci in pellentesque condimentum.  Praesent dui lorem, egestas a
        urna ac, posuere posuere nisl. Curabitur ut ipsum consequat, commodo tortor
        egestas, elementum nisi. Quisque euismod, enim id aliquet tincidunt, justo massa
        iaculis sapien, nec cursus lorem nisi sit amet ipsum. Fusce at sollicitudin ligula.
        Maecenas lobortis ante vel est interdum, vitae vehicula ligula consequat. Nullam
        tempus, purus quis suscipit sodales, felis ex efficitur massa, vitae eleifend nibh
        erat sed dui. Etiam dolor risus, euismod ut mauris vitae, bibendum ullamcorper
        odio. In at dictum metus. Curabitur suscipit, eros eu consectetur malesuada, lacus
        quam accumsan enim, sed laoreet orci risus at nisi.  Phasellus cursus, lorem nec
        fermentum sollicitudin, tortor mauris placerat velit, ut bibendum nisi justo ut ligula.
        Etiam ut metus tempus, lacinia tortor ut, dictum arcu. In sagittis leo ipsum, et
        pulvinar odio tincidunt at. Nullam ullamcorper ipsum sed tellus vehicula, in
        consectetur massa malesuada. Integer et massa diam. Morbi luctus id nisi quis tincidunt.
        Ut rutrum pretium nibh. Suspendisse euismod pharetra orci, in iaculis diam
        facilisis ut. Sed sed lorem ullamcorper, malesuada lacus ut, pulvinar enim. Duis at
        congue augue, a finibus est. Sed lacinia vitae arcu a tristique. Praesent at
        consequat lectus. Sed sed orci non nunc molestie commodo eu vel purus. Nulla ligula
        libero, lobortis et diam eget, pretium varius leo.  Suspendisse ullamcorper magna in
        ante bibendum sodales. Quisque fermentum, eros quis gravida auctor, enim eros
        cursus lectus, vel pretium libero sem id sapien. Vestibulum malesuada nibh nisi, sed
        feugiat diam tincidunt at. Donec pellentesque dolor justo, et vulputate libero aliquam
        sit amet. Nullam congue semper dolor et sagittis. Nullam sit amet nibh arcu.
        Vivamus eu finibus augue. Vivamus accumsan nibh vel sem efficitur, quis ornare libero
        eleifend. Phasellus feugiat nisi tellus, id elementum mi pretium ut.  Curabitur vehicula
        est vel rutrum auctor. Integer ut mollis erat. Vestibulum porttitor consequat
        libero, placerat venenatis sem vestibulum sit amet. Phasellus vulputate quam non nisl
        finibus fringilla. Mauris congue dolor at ipsum semper tempor. Pellentesque ligula. 
    );
    #undef STRINGIFY
    
    if(size <= 0)
        return;

    //Crete variations of the above text by 'rotating' letters more and more
    // with each subsequent repetition of the text.
    int long_string_base_size = (int) strlen(long_string_base);
    for(int i = 0; i < size; i += long_string_base_size)
    {
        int remaining = long_string_base_size;
        if(remaining > size - i)
            remaining = size - i;

        for(int j = 0; j < remaining; j++)
        {
            char c = long_string_base[j];

            //If c is a letter 'rotate' it forward. That is turn:
            // a -> b; b -> c; ..... z -> a; 
            if('a' <= c && c <= 'z')
                into[i + j] = (c - 'a' + i) % ('z' - 'a') + 'a';
            //Uppercase get rotated by i/36 so that the text does no repeat for at least 36^2 
            //copies. This means we can generate up to 36*36*4096 = 5.3 MB of non repeating data
            else if('A' <= c && c <= 'Z')    
                into[i + j] = (c - 'a' + i/36) % ('z' - 'a') + 'a';
            else
                into[i + j] = c;
        }
    }
    into[size - 1] = '\0';
}
#endif