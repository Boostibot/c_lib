#pragma once

#include "string.h"
#include "arena_stack.h"

typedef enum JSON_Type {
    JSON_NULL = 0,
    JSON_BOOL = 1,
    JSON_NUMBER = 2,
    JSON_STRING = 3,
    JSON_COMMENT = 4,
    JSON_ARRAY = 5,
    JSON_OBJECT = 6,
} JSON_Type;

typedef union JSON_Val JSON_Val;
typedef struct JSON_Object_Entry JSON_Object_Entry;

typedef struct JSON_Object {
    JSON_Type type;
    uint32_t count; 
    JSON_Object_Entry* items; //followed by array of hash table lookups
} JSON_Object;

typedef struct JSON_String {
    JSON_Type type;
    uint32_t count;
    const char* data;
} JSON_String;

typedef JSON_String JSON_Comment;

typedef struct JSON_Array {
    JSON_Type type;
    uint32_t count; 
    JSON_Val* items; 
} JSON_Array;

typedef union JSON_Val {
    JSON_Type type; //always availible
    JSON_String string;
    JSON_Comment comment;
        
    struct {
        JSON_Type _1;
        uint32_t _1;
        f64 number; 
    };
    struct {
        JSON_Type _2;
        uint32_t _2;
        bool boolean;
    };

    JSON_Array array;
    JSON_Object object;
} JSON_Val;

typedef struct JSON_Object_Entry {
    JSON_Val key;
    JSON_Val value;
} JSON_Object_Entry;

#include "hash_func.h"
uint32_t json_hash(JSON_Val val)
{
    if(val.type == JSON_STRING || val.type == JSON_COMMENT)
        return hash32_fnv(val.string.data, val.string.count, 0);
    if(val.type == JSON_NUMBER || val.type == JSON_BOOL)
        return hash64_fold(hash64_bijective(val.number));
    return 0;
}

typedef enum JSON_Token_Type {
    JSON_TOKEN_ERROR = 0,
    JSON_TOKEN_NUMBER,
    JSON_TOKEN_BOOL,
    JSON_TOKEN_NULL,
    JSON_TOKEN_ID,
    JSON_TOKEN_STRING,
    JSON_TOKEN_SPACE,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_COMMENT,
    JSON_TOKEN_COMMENT_MULTILINE,
    JSON_TOKEN_COLON,
    JSON_TOKEN_SEMICOL,
    JSON_TOKEN_ARR_BEGIN,
    JSON_TOKEN_ARR_END,
    JSON_TOKEN_OBJ_BEGIN,
    JSON_TOKEN_OBJ_END,
    JSON_TOKEN_EOF,
} JSON_Token_Type;

typedef struct JSON_Token {
    isize from;
    isize to;
    JSON_Token_Type type;
    u32 _;
    f64 num_value;
    String string_value;
} JSON_Token;

bool string_has_char_at(String str, char c, isize at)
{
    if(at >= str.count)
        return false;
    return str.data[at] == c;
}

char string_at_or(String str, isize at, char if_out_of_range)
{
    if(at >= str.count)
        return if_out_of_range;
    return str.data[at];
}

typedef enum JSON_Error_Kind {
    JSON_LEXING_ERROR,
    JSON_LEXING_ERROR_NUMBER_FORMAT,
    JSON_LEXING_ERROR_STRING_FORMAT,
    JSON_LEXING_ERROR_ARRAY_FORMAT,
    JSON_LEXING_ERROR_OBJECT_FORMAT,
} JSON_Error_Kind;

typedef struct JSON_Error {
    isize offset;
    isize line;
    isize column;
    String error_text;
} JSON_Error;

typedef struct JSON_Context {
    isize max_depth;
    Arena_Frame* arena_out;
    Arena_Frame* arena_temp;
    Arena_Frame* arena_error;
} JSON_Context;

typedef struct JSON_Parse_Result {
    bool state;
    JSON_Token last_token;
    JSON_Val value;
    isize errors_from;
    isize errors_count;
} JSON_Parse_Result;

typedef struct JSON_Link_Val{
    struct JSON_Link_Val* next;
    struct JSON_Link_Val* prev;
    JSON_Val val;
} JSON_Link_Val;

char* json_string_allocate(JSON_Context* context, isize size);
void* json_mem_allocate(JSON_Context* context, isize size);

void json_link_val_push_back(JSON_Context* context, JSON_Link_Val** head, JSON_Link_Val** tail, JSON_Val val);
bool json_link_val_pop_front(JSON_Context* context, JSON_Link_Val** head, JSON_Link_Val** tail, JSON_Val* val);

JSON_Link_Val* json_link_val_allocate(JSON_Context* context);
void json_link_val_deallocate(JSON_Context* context, JSON_Link_Val* link);

void json_report_parse_error(isize at, const char* error, ...)
{
    
}

bool char_is_json_id_start(char c)
{
    return char_is_alphabetic(c) || c == '_' || c == '$';
}

bool char_is_json_id(char c)
{
    return char_is_alphabetic(c) || char_is_digit(c) || c == '_' || c == '$';
}

char* json_inv_base_map()
{
    static volatile u8 inv_base_map_init = 0;
    static u8 inv_base_map[256] = {0};
    if(inv_base_map_init == 0)
    {
        memset(inv_base_map, -1, sizeof(inv_base_map));
        inv_base_map['0'] = 0;
        inv_base_map['1'] = 1;
        inv_base_map['2'] = 2;
        inv_base_map['3'] = 3;
        inv_base_map['4'] = 4;
        inv_base_map['5'] = 5;
        inv_base_map['6'] = 6;
        inv_base_map['7'] = 7;
        inv_base_map['8'] = 8;
        inv_base_map['9'] = 9;
        inv_base_map['A'] = 10;
        inv_base_map['B'] = 11;
        inv_base_map['C'] = 12;
        inv_base_map['D'] = 13;
        inv_base_map['E'] = 14;
        inv_base_map['F'] = 15;
        inv_base_map['a'] = 10;
        inv_base_map['b'] = 11;
        inv_base_map['c'] = 12;
        inv_base_map['d'] = 13;
        inv_base_map['e'] = 14;
        inv_base_map['f'] = 15;

        inv_base_map_init = 1;
    }

    return inv_base_map;
}


u64 _json_parse_uint(String input, isize from, isize* to, u8 base)
{
    u64 value = 0;
    isize at = from;
    if(base == 10)
    {
        for(; at < input.count; at ++)
        {
            char c = input.data[at];
            u8 val = (u8) (c - '0');
            if(val >= 10)
                break;
            else
            {
                u64 new_value = value * 10 + val;
                if(new_value < value)
                    value = UINT64_MAX;
                else
                    value = new_value;
            }
        }
    }
    else
    {
        u64 base_64 = base;
        u8* base_map = json_inv_base_map();
        for(; at < input.count; at ++)
        {
            char c = input.data[at];
            u8 val = base_map[c];
            if(val >= base)
                break;
            else
            {
                u64 new_value = value*base_64 + val;
                if(new_value < value)
                    value = UINT64_MAX;
                else
                    value = new_value;
            }
        }
    }

    *to = at;
    return value;
}
i64 _json_parse_int(String input, isize from, isize *to, u8 base)
{
    isize at = from;
    char first_c = input.data[at];
    i64 sign = 0;
    if(first_c == '-')
    {
        sign = -1;
        at += 1;
    }
    else if(first_c == '+')  
    {
        sign = 1;
        at += 1;
    }

    u64 uvalue = _json_parse_uint(input, at, to, base);
    i64 value = 0;
    if(sign)
    {
        if(uvalue >= INT64_MAX)
            value = INT64_MAX;
        else
            value = (i64) uvalue;
    }
    else
    {
        if(uvalue >= -INT64_MIN)
            value = INT64_MIN;
        else
            value = -(i64) uvalue;
    }
    
    return value;
}


JSON_Token json_tokenize_id(JSON_Context* context, String input, isize from)
{
    JSON_Token out = {0};

    isize at = from;
    char first_c = string_at_or(input, at, ' ');
    if(char_is_json_id_start(first_c))
    {
        at += 1;
        for(; at < input.count; at ++)
        {
            char c = input.data[at];
            if(char_is_json_id(c) == false) 
                break;
        }

        out.type = JSON_TOKEN_ID;
    }
    else
    {
        out.type == JSON_TOKEN_ERROR;
    }
    
    out.from = from;
    out.to = at;
    return out;
}

JSON_Token json_tokenize_number(JSON_Context* context, String input, isize from)
{
    JSON_Token out = {0};
    ASSERT(from < input.count);

    isize at = from;
    char sign_c = input.data[at];

    //Resolve sign
    f64 sign = 0;
    if(sign_c == '-')
    {
        sign = -1;
        at += 1;
    }
    if(sign_c == '+')  
    {
        sign = 1;
        at += 1;
    }
            
    u8 base = 10;

    //determine base encoding
    if(string_at_or(input, at, ' ') == '0')
    {
        char base_id = string_at_or(input, at + 1, ' ');
        if(base_id == 'x')
        {
            base = 16;
            at += 2;
        }
        else if(base_id == 'o')
        {
            base = 8;
            at += 2;
        }
        else if(base_id == 'b')
        {
            base = 2;
            at += 2;
        }
    }

    //get number without dot (mantissa) and the dot positions (dot_exp)
    f64 mantissa = 0;
    i64 dot_exp = 0;

    isize before_mantissa_pos = at;
    isize had_dot_times = false;
    if(base == 10)
    {
        u64 value = 0;
        u64 has_dot = 0;
        u64 extra_exp = 0;
        u64 after_dot = 0;
        for(; at < input.count; at ++)
        {
            char c = input.data[at];
            u8 val = (u8) (c - '0');
            if(val >= 10)
            {
                if(c != '.')
                    break;

                if(had_dot_times++)
                    break;

                has_dot = true;
            }
            else
            {
                u64 new_value = value * 10 + val;
                if(new_value < value)
                {
                    //if first digit after max round
                    if(extra_exp == 0 && val >= 5)
                    {
                        value += 1;
                        ASSERT(value != 0);
                    }

                    extra_exp += 1;
                }
                else
                    value = new_value;

                after_dot += has_dot;
            }
        }
            
        mantissa = (f64) value;
        dot_exp = extra_exp - after_dot;
    }
    else
    {
        u8* inv_base_map = json_inv_base_map();
        u64 has_dot = 0;
        u64 after_dot = 0;

        f64 base_f64 = 2;
        f64 value = 0;
        for(; at < input.count; at ++)
        {
            char c = input.data[at];
            u8 val = inv_base_map[(u8) c];
            if(val >= base)
            {
                if(c != '.')
                    break;

                if(had_dot_times++)
                    break;

                has_dot = true;
            }
            else
            {
                value *= base_f64;
                value += val;
            }
            
            after_dot += has_dot;
        }

        mantissa = value;
        dot_exp = -after_dot;
    }
        
    isize after_mantissa_pos = at;
    isize mantissa_size = after_mantissa_pos - before_mantissa_pos;

    //If zero sized check for +- infinity, else error and exit
    if(mantissa_size == 0)
    {
        JSON_Token id = json_tokenize_id(context, input, at);
        String possibly_inf = string_range(input, id.from, id.to);
        if(string_is_equal(possibly_inf, STRING("Infinity")) || string_is_equal(possibly_inf, STRING("inf")))
        {
            out.from = from;
            out.to = id.to;
            out.type = JSON_TOKEN_NUMBER;
            out.num_value = (sign == 0 ? 1 : sign)*INFINITY;
        }
        else if(possibly_inf.count > 0)
            json_report_parse_error(at, "Stranded number sign followed by '%.*s'", (int) possibly_inf.count, possibly_inf.data);
        else
            json_report_parse_error(at, "Stranded number sign followed by '%c'", input.data[at]);
    }
    //continue
    else
    {
        //Report some errors
        if (had_dot_times >= 2)
            TODO("ERROR two dots");

        if (had_dot_times >= 1 && mantissa_size == 1)
            TODO("ERROR just dot not allowed");

        //Get exponent if indicated
        i64 user_exp = 0;
        if(mantissa_size >= 1)
        {
            char exp_indicator = string_at_or(input, at, ' ');
            if(exp_indicator == 'e' || exp_indicator == 'E')
            {
                at += 1;
                char exp_sign_c = string_at_or(input, at, ' ');

                i64 exp_sign = 0;
                if(exp_sign_c == '-')
                {
                    exp_sign = -1;
                    at += 1;
                }
                else if(exp_sign_c == '+')  
                {
                    exp_sign = 1;
                    at += 1;
                }
                
                //does not really matter just dont want the value to overflow...
                i64 MAX_EXP = (i64) 1000*1000*1000*1000;
                
                isize exp_digits = 0;
                u8* base_map = json_inv_base_map();
                for(; at < input.count; at ++, exp_digits++)
                {
                    char c = input.data[at];
                    u8 val = base_map[c];
                    if(val >= base)
                        break;
                    else if(user_exp <= MAX_EXP)
                        user_exp = user_exp*base + val;
                }

                if(exp_digits == 0)
                {
                    TODO("Error zero sized exp");
                }
            
                user_exp *= (exp_sign == 0 ? 1 : exp_sign);
            }
        }
        
        //After number must not be an identifier
        char after = string_at_or(input, at, ' ');
        if(char_is_json_id_start(after))
        {
            TODO("ERROR missing space");
        }

        f64 exponent = (f64) (user_exp + dot_exp);
        f64 exp_mul = 0;
        if(base == 10)
            exp_mul = pow(5, exponent)*exp2(exponent);
        else if(base == 2)
            exp_mul = exp2(exponent);
        else if(base == 8)
            exp_mul = exp2(3*exponent);
        else if(base == 16)
            exp_mul = exp2(4*exponent);
        else if(base == 64)
            exp_mul = exp2(6*exponent);
        else
            exp_mul = pow((f64) base, exponent);

        f64 out_val = (sign == 0 ? 1 : sign) * mantissa * exp_mul;

        out.from = from;
        out.to = at;
        out.num_value = out_val;
        out.type = JSON_TOKEN_NUMBER;
    }

    return out;
}

JSON_Token json_tokenize_string(JSON_Context* context, Arena_Frame* arena, String input, isize from)
{
    ASSERT(string_has_char_at(input, '"', from) || string_has_char_at(input, '\'', from));
    char quote_type = input.data[from];

    isize token_from = from;
    isize token_to = input.count;

    String_Builder builder = {arena->alloc};

    bool terminated = false;
    bool was_escaped = false;
    for(isize i = token_from + 1; i < input.count; i++)
    {
        char c = input.data[i];
        if(was_escaped)
        {
            bool push = true;
            char pushed = '\0';
            switch(c)
            {
                case '"':  pushed = '"'; break;
                case '\'': pushed = '\''; break;
                case 'n':  pushed = '\n'; break;
                case 't':  pushed = '\t'; break;
                case '\\': pushed = '\\'; break;
                case '/':  pushed = '/'; break;
                case '\n':  pushed = '\n'; break;
                case 'b':  pushed = '\b'; break;
                case 'r':  pushed = '\r'; break;
                case 'v':  pushed = '\v'; break;
                case 'f':  pushed = '\f'; break;
                case '0':  pushed = '\0'; break;
                case 'u': {
                    if(i + 4 >= input.count)
                    {
                        TODO("error"); 
                        i = input.count;
                    }
                    else
                    {
                        char* base_map = json_inv_base_map();
                        u8 vals[4] = {0};

                        u8 bad_nums = 0;
                        for(isize k = 0; k < 4; k++)
                        {
                            char c_unicode = input.data[i + 1 + k];
                            u8 val = base_map[c_unicode];
                            bad_nums += val >= 16;
                                
                            vals[k] = val;
                        }

                        if(bad_nums)
                        {
                            TODO("error"); 
                        }

                        u16 unicode = vals[0] | vals[1] << 4 | vals[2] << 8 | vals[3] << 12;

                    }
                    
                    TODO("hex code"); 
                    push = false;
                } break;

                default: {
                    TODO("ERROR"); 
                    push = false;
                }
            }

            if(push)
                builder_push(&builder, pushed);

            was_escaped = false;
        }
        else 
        {
            if(c == '\\')
                was_escaped = true;
            else if(c == quote_type)
            {
                terminated = true;
                token_to = i + 1;
                break;
            }
            else if(c == '\n')
            {
                TODO("ERROR"); 
                token_to = i;
                break;
            }
            else
                builder_push(&builder, c);
        }
    }
    
    JSON_Token token = {0};
    token.from = token_from;
    token.from = token_to;
    token.type = JSON_TOKEN_STRING;
    token.string_value = builder.string;

    if(terminated == false)
    {
        TODO("ERROR");
    }
    else
    {
        ASSERT(was_escaped == false);
    }

    return token;
}

JSON_Token json_get_token(JSON_Context* context, String input, isize at)
{
    JSON_Token out = {0};

    out.from = at;
    out.to = at + 1;
    if(at >= input.count)
    {
        out.type = JSON_TOKEN_EOF;
        out.to = at;
        return out;
    }

    bool continue_loop = true;
    char first_c = input.data[at];
    switch(input.data[at])
    {
        case '.': 
        case '-': 
        case '+': 
        case '0': 
        case '1': 
        case '2': 
        case '3': 
        case '4': 
        case '5': 
        case '6': 
        case '7': 
        case '8': 
        case '9': {
            return json_tokenize_number(context, input, at);
        } break;

        case '\'': 
        case '"': {
            return json_tokenize_string(context, context->arena_temp, input, at);
        } break;
            
        case ' ': 
        case '\t': 
        case '\v': 
        case '\f': 
        case '\r': 
        case '\n': {
            f64 newline_count = 0;
            for(; out.to < input.count; out.to ++)
            {
                char c = input.data[out.to];
                if(c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r')
                    continue;
                if(c == '\n')
                    newline_count += 1;
                else
                    break;
            }

            out.type = JSON_TOKEN_SPACE;
            out.num_value = newline_count;
            return out;
        } break;
        
        case ',': {
            out.type = JSON_TOKEN_COMMA;
            return out;
        } break;
        
        case ':': {
            out.type = JSON_TOKEN_SEMICOL;
            return out;
        } break;

        case '/': {
            char next_c = string_at_or(input, at + 1, ' ');
            if(next_c == '/')
            {
                out.to = string_find_first_char(input, '\n', at + 2);
                if(out.to == -1)
                    out.to = input.count;

                out.type = JSON_TOKEN_COMMENT;
            }
            else if(next_c == '*')
            {
                isize depth = 1;
                isize curr = at + 2;
                for(;;) {
                    isize next_start = string_find_first_or(input, STRING("/*"), curr, input.count);
                    isize next_end = string_find_first_or(input, STRING("*/"), curr, input.count);

                    next_start += STRING("/*").count;
                    next_end += STRING("*/").count;

                    if(next_start < next_end)
                        depth += 1;
                    else
                        depth -= 1;

                    curr = next_start < next_end ? next_start : next_end;
                    curr = curr < input.count ? curr : input.count;
                    if(depth == 0 || curr == input.count)
                        break;
                }

                out.to = curr;
                out.type = JSON_TOKEN_COMMENT_MULTILINE;
            }

            return out;
        } break;

        default: {
            out = json_tokenize_id(context, input, at);

            //named constants
            String id = string_range(input, out.from, out.to);
            if(first_c == 't' && string_is_equal(id, STRING("true")))
            {
                out.type = JSON_TOKEN_BOOL;
                out.num_value = 1;
            }
            else if(first_c == 'f' && string_is_equal(id, STRING("false")))
            {
                out.type = JSON_TOKEN_BOOL;
                out.num_value = 0;
            }
            else if(first_c == 'n' && string_is_equal(id, STRING("null")))
            {
                out.type = JSON_TOKEN_NULL;
            }
            else if(first_c == 'N' && string_is_equal(id, STRING("NaN")))
            {
                out.type = JSON_TOKEN_NUMBER;
                out.num_value = nan("");
            }
            else if(first_c == 'n' && string_is_equal(id, STRING("nan")))
            {
                out.type = JSON_TOKEN_NUMBER;
                out.num_value = nan("");
            }
            else if(first_c == 'I' && string_is_equal(id, STRING("Infinity")))
            {
                out.type = JSON_TOKEN_NUMBER;
                out.num_value = INFINITY;
            }
            else if(first_c == 'i' && string_is_equal(id, STRING("inf")))
            {
                out.type = JSON_TOKEN_NUMBER;
                out.num_value = INFINITY;
            }
        }
    }

    return out;
}


JSON_Token json_get_content_token(JSON_Context* context, String input, isize from)
{
    isize at = from;
    for(;;) {
        JSON_Token token = json_get_token(context, input, at);
        if(token.type != JSON_TOKEN_SPACE && token.type != JSON_TOKEN_COMMENT && token.type != JSON_TOKEN_COMMENT_MULTILINE)
            return token;

        at = token.to;
    }
}

JSON_Object json_object_make(JSON_Context* context, const JSON_Link_Val* keys, const JSON_Link_Val* values)
{

}

isize json_object_get_index(JSON_Object object, JSON_Val key)
{
    
}

JSON_Val* json_object_get(JSON_Object object, JSON_Val key)
{

}

bool json_object_set(JSON_Context* context, JSON_Object object, JSON_Val key, JSON_Val value)
{

}
bool json_object_remove(JSON_Object object, JSON_Val key)
{

}

JSON_String json_string_make(JSON_Context* context, const char* data, isize size);

JSON_Parse_Result json_parse_value(JSON_Context* context, String input, isize from, isize depth)
{
    JSON_Parse_Result out = {0};
    if(depth >= context->max_depth)
    {
        TODO("ERROR");
        return out;
    }

    isize at = from;
    JSON_Token first_token = json_get_content_token(context, input, at);
    if(first_token.type == JSON_TOKEN_STRING)
    {
        out.value.string = json_string_make(context, first_token.string_value.data, first_token.string_value.count);
    }
    else if(first_token.type == JSON_TOKEN_NUMBER)
    {
        out.value.type = JSON_NUMBER;
        out.value.number = first_token.num_value;
    }
    else if(first_token.type == JSON_TOKEN_BOOL)
    {
        out.value.type = JSON_BOOL;
        out.value.boolean = first_token.num_value ? true : false;
    }
    else if(first_token.type == JSON_TOKEN_NULL)
    {
        out.value.type = JSON_NULL;
    }
    else if(first_token.type == JSON_TOKEN_OBJ_BEGIN)
    {
        for(;;) {
            JSON_Token token = json_get_content_token(context, input, at);
            if(token.type == JSON_TOKEN_OBJ_END)
                break;

            if(token.type == JSON_TOKEN_EOF)
                break;
        }
    }
    else if(first_token.type == JSON_TOKEN_ARR_BEGIN)
    {
        JSON_Link_Val* head = NULL;
        JSON_Link_Val* tail = NULL;
        isize values_count = 0;

        at = first_token.to;
        for(;;) {
            JSON_Parse_Result item = json_parse_value(context, input, at, depth + 1);
            JSON_Token next_token = {0};
            if(item.state)
            {   
                at = item.last_token.to;

                json_link_val_push_back(context, &head, &tail, item.value);
                values_count += 1;
                next_token = json_get_content_token(context, input, at);
            }
            else
                next_token = item.last_token;

            if(next_token.type == JSON_TOKEN_ARR_END)
                break;
            else if(next_token.type == JSON_TOKEN_EOF)
            {
                TODO("ERROR but exit"); 
                break;
            }
            else if(next_token.type != JSON_TOKEN_COMMA)
            {
                TODO("ERROR but recover"); 
            }
        }

        JSON_Val* values = (JSON_Val*) json_mem_allocate(context, values_count*sizeof(JSON_Val));
        for(isize i = 0; i < values_count; i++)
        {
            JSON_Val val = {0};
            bool has_val = json_link_val_pop_front(context, &head, &tail, &values[i]);
            ASSERT(has_val);
        }

        ASSERT(values_count <= UINT32_MAX);
        out.value.type = JSON_ARRAY;
        out.value.array.items = values;
        out.value.array.count = (uint32_t) values_count;
    }

    return out;
}


JSON_Parse_Result json_parse(JSON_Context* context, String input)
{
    return json_parse_value(context, input, 0, 0);
}

void json_parse_element(JSON_Context* context, String input, isize at, isize depth)
{
    
}

void _json_parse(String input)
{
    isize at = 0;
    for(;;) {
        JSON_Token token = json_get_token(input, at);
        

        switch(token.type)
        {
            
        }
        if(token.type == JSON_TOKEN_EOF)
            break;

        at = token.to + 1;
    }
}
