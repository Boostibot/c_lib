#pragma once

#include "string.h"
#include "arena.h"

typedef enum JSON_Type {
    JSON_NONE = 0,
    //Number representation
    JSON_BOOL = 8,
    JSON_NUMBER = 16, //f64

    //String representation
    JSON_STRING_REP = 24,
    JSON_STRING = 24,
    JSON_COMMENT = 24 | 1,

    //Array representation
    JSON_ARRAY_REP = 32,
    JSON_ARRAY = 32,
    JSON_OBJECT = 32 | 1,
} JSON_Type;

typedef struct JSON_Link_Val{
    
} JSON_Link_Val;

typedef struct JSON_Link_Obj{
    struct JSON_Link_Obj* next;

    JSON_Type type;
    union {
        String string, comment;
        
        struct {
            struct JSON_Link_Obj* first; 
            struct JSON_Link_Obj* last; 
        } array, object;

        f64 number, _bool;
    };


} JSON_Link_Obj;

typedef enum JSON_Token_Type {
    JSON_TOKEN_ERROR = 0,
    JSON_TOKEN_NUMBER,
    JSON_TOKEN_BOOL,
    JSON_TOKEN_STRING,
    JSON_TOKEN_SPACE,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_COMMENT,
    JSON_TOKEN_COMMENT_MULTILINE,
    JSON_TOKEN_NEWLINE,
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
    u32 pad;
    f64 num_value;
} JSON_Token;

JSON_Token json_get_token(String input, isize at)
{
    JSON_Token out = {0};
    out.from = at;
    out.to = at + 1;
    if(at >= input.len)
    {
        out.type = JSON_TOKEN_EOF;
        out.to = at;
        return out;
    }
        
    bool continue_loop = true;
    char first_c = input.data[at];
    switch(input.data[at])
    {
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
            out.type = JSON_TOKEN_NUMBER;
            if(out.to >= input.len)
            {
                if(first_c == '-' || first_c == '+')
                    out.type = JSON_TOKEN_ERROR;
                    
                return out;
            }

            isize from = at;
            f64 sign = 1;
            if(first_c == '-')
            {
                sign = -1;
                out.to += 1;
                from += 1;
            }
            if(first_c == '+')  
            {
                out.to += 1;
                from += 1;
            }
                
            char second_c = input.data[from];
            if(second_c == '0' && out.to + 1 < input.len)
            {
                char third = input.data[out.to + 1];
                if(third == 'x' || third == 'b' || third == 'o')
                {
                
                }
            }

            for(; out.to < input.len; out.to ++)
            {
                char c = input.data[out.to];
                if(char_is_digit(c) == false)
                {
                    if(c == '.')
                        
                }
            }

              

            switch(second_c)
            {
                case 'x':
                case 'o':
                case 'b':

                default: {
                            
                } break;
            }

            //hex
            if(second_c == 0 && second_c == 'x')
            {
            }
            //bin
            if(second_c == 0 && second_c == 'x')
            {
            }
            //oct
            if(second_c == 0 && second_c == 'o')
            {
            }
            else
            {
            
            }

        } break;

        case '\'': 
        case '"': {
            out.type = JSON_TOKEN_STRING;

            bool is_double = first_c == '"';
            bool found_end = false;
            for(; out.to < input.len; out.to ++)
            {
                
            }


        } break;
            
        case ' ': 
        case '\t': 
        case '\v': 
        case '\f': 
        case '\r': {
            for(; out.to < input.len; out.to ++)
            {
                char c = input.data[out.to];
                if(c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r')
                    continue;
                else
                    break;
            }

            out.type = JSON_TOKEN_SPACE;
            return out;
        } break;

        case '\n': {
            out.type = JSON_TOKEN_NEWLINE;
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
            if(input.len > out.to)
            {
                char c = input.data[out.to];
                if(c == '/')
                {
                    out.type = JSON_TOKEN_COMMENT;
                    
                }
                if(c == '*')
                {
                    out.type = JSON_TOKEN_COMMENT_MULTILINE;
                }
            }

            return out;
        } break;

        default: {
            if(char_is_alphabetic(input.data[at]) || input.data[at] == '_' || input.data[at] == '$')
            {
                for(; out.to < input.len; out.to ++)
                {
                    char c = input.data[out.to];
                    if(char_is_alphabetic(c) || char_is_digit(c) || c == '_' || c == '$') 
                        {}
                    else
                        break;
                }
            }
            else
            {
                out.type == JSON_TOKEN_ERROR;
                return out;
            }
        }
    }


    if(out.type == JSON_TOKEN_EOF)
    {
        
    }
}

void json_parse(String input)
{
    
}
