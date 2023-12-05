#ifndef JOT_FORMAT_NETBPM
#define JOT_FORMAT_NETBPM

// This file contains readers and writers for some of the more common variants of the netbmp formats.
// These formats are really simple and have no data compression at all. This makes them ideal for really fast loading and writing.
//
// The formats are summarised below. For some of these exist both ascii and binary variants. We only parse binary variants.
//
// EXTENSION    ACII MAGIC  BINARY MAGIC  COLORS AND FORMAT                                  
// .pbm         P1          P4            [0..1] where 0 is white and 1 is black; the binary variant has the first pixel in the highest position of the byte
// .pgm         P2          P5            [0..255], or [0..65535] in big endian; grayscale image
// .ppm         P3          P6            [0..255]^3 or [0..65535]^3 in big endian; rgb image
// .pfm         --          Pf            [0, r] where r is any number; float grayscale image; We call this format pfmg for pfm grayscale
// .pfm         --          PF            [0, r]^3 where r is any number; float rgb image
// .pam         --          P7            [0..65535]^channels where channels is any number.
//
// The ascii vriants have the same header as the binay variants. They differ only in the format of the actual data.
// 
// The ascii files have data comprised of numbers in decimal separated by whitespace. There is one number for one channel.
// The binary files have data simply layed out in memory next to each other in row major order. There is no compression.

#include "image.h"
#include "string.h"
#include "format.h"
#include "error.h"

typedef enum Netbpm_Format {
    NETBPM_FORMAT_NONE = 0,
    NETBPM_FORMAT_PBM_ASCII = 1,
    NETBPM_FORMAT_PGM_ASCII = 2,
    NETBPM_FORMAT_PPM_ASCII = 3,
    NETBPM_FORMAT_PBM = 4,
    NETBPM_FORMAT_PGM = 5,
    NETBPM_FORMAT_PPM = 6,
    NETBPM_FORMAT_PAM = 7,
    NETBPM_FORMAT_PFM = 8,
    NETBPM_FORMAT_PFMG = 9,
} Netbpm_Format;

typedef enum Netbpm_Format_Error {
    NETBPM_FORMAT_ERROR_NONE = 0,
    NETBPM_FORMAT_ERROR_BAD_TYPE,
    NETBPM_FORMAT_ERROR_INVALID_HEADER,
    NETBPM_FORMAT_ERROR_INVALID_HEADER_VALUES,
    NETBPM_FORMAT_ERROR_NOT_ENOUGH_DATA,
} Netbpm_Format_Error;

typedef enum Endian {
    ENDIAN_UNKNOWN = 0,
    ENDIAN_LITTLE = 1,
    ENDIAN_BIG = 2,
} Endian;

EXPORT Endian endian_get_local();
EXPORT u32 endian_byteswap(u32 val);
EXPORT Netbpm_Format netbpm_format_classify(String data);

EXPORT Error netbpm_format_pgm_write_into(String_Builder* into, Image image);
EXPORT Error netbpm_format_pgm_read_into(Image_Builder* image, String ppm);

EXPORT Error netbpm_format_ppm_write_into(String_Builder* into, Image image);
EXPORT Error netbpm_format_ppm_read_into(Image_Builder* image, String ppm);

EXPORT Error netbpm_format_pfmg_write_into(String_Builder* into, Image image, f32 range);
EXPORT Error netbpm_format_pfmg_read_into(Image_Builder* image, String ppm);

EXPORT Error netbpm_format_pfm_write_into(String_Builder* into, Image image, f32 range);
EXPORT Error netbpm_format_pfm_read_into(Image_Builder* image, String ppm);

EXPORT Error netbpm_format_pam_write_into(String_Builder* into, Image image);
EXPORT Error netbpm_format_pam_read_into(Image_Builder* image, String ppm);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FORMAT_NETBPM_IMPL)) && !defined(JOT_FORMAT_NETBPM_HAS_IMPL)
#define JOT_FORMAT_NETBPM_HAS_IMPL

EXPORT u32 endian_byteswap(u32 val)
{
    union Swapper {
        u32 val;
        u8 vals[4];
    };
    
    union Swapper original = {val};
    union Swapper swapped = {0};
    swapped.vals[0] = original.vals[3];
    swapped.vals[1] = original.vals[2];
    swapped.vals[2] = original.vals[1];
    swapped.vals[3] = original.vals[0];

    return swapped.val;
}

EXPORT Netbpm_Format netbpm_format_classify(String data)
{
    if(data.size < 3 || data.data[0] != 'P' || data.data[2] != '\n')
        return NETBPM_FORMAT_NONE;
 
    switch(data.data[1])
    {
        case '1': return NETBPM_FORMAT_PBM_ASCII;
        case '2': return NETBPM_FORMAT_PGM_ASCII;
        case '3': return NETBPM_FORMAT_PPM_ASCII;
        case '4': return NETBPM_FORMAT_PBM;
        case '5': return NETBPM_FORMAT_PGM;
        case '6': return NETBPM_FORMAT_PPM;
        case '7': return NETBPM_FORMAT_PAM;

        case 'F': return NETBPM_FORMAT_PFM;
        case 'f': return NETBPM_FORMAT_PFMG;
        default: return NETBPM_FORMAT_NONE;
    }
}

INTERNAL const char* format_ppm_translate_error(u32 code, void* context)
{
    (void) context;
    switch(code)
    {
        default: return ERROR_SYSTEM_STRING_UNEXPECTED_ERROR;
        case NETBPM_FORMAT_ERROR_INVALID_HEADER: return "invalid header of ppm file";
        case NETBPM_FORMAT_ERROR_INVALID_HEADER_VALUES: return "values found in ppm header are invalid (negative or too big)";
        case NETBPM_FORMAT_ERROR_NOT_ENOUGH_DATA: return "not enough data to fill all width * height pixels of the file";
    }
}

INTERNAL u32 netbmp_format_error_module()
{
    static u32 error_module = 0;
    if(error_module == 0)
        error_module = error_system_register_module(format_ppm_translate_error, "format_ppm.h", NULL);

    return error_module;
}

EXPORT Endian endian_get_local()
{
    union {
        u8 vals[4];
        u32 val;
    } endian_tester = {{0x11, 0x22, 0x33, 0x44}};

    switch(endian_tester.val)
    {
        case 0x11223344: return ENDIAN_BIG;
        case 0x44332211: return ENDIAN_LITTLE;
        default:         return ENDIAN_UNKNOWN;
    }
}


INTERNAL Error _netbpm_format_write_append_pgm_ppm(String_Builder* append_into, Image image, const char* magic, i32 channels)
{
    if(image.pixel_format != PIXEL_FORMAT_U8 && image_channel_count(image) != channels)
        return error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_BAD_TYPE);

    isize pixel_count = image.width * image.height;
    isize neeed_size = pixel_count * channels;
    
    const int max_value = 255;
    array_reserve(append_into, append_into->size + neeed_size + 40);

    format_append_into(append_into, "%s\n%d %d\n%d\n", magic, (int) image.width, (int) image.height, max_value);

    isize size_before = append_into->size;
    array_resize(append_into, size_before + neeed_size);

    u8* dest = (u8*) append_into->data + size_before;
    if(image_is_contiguous(image))
        memcpy(dest, image.pixels, neeed_size);
    else
    {
        isize line_bytes = image.width * image.pixel_size;
        for(isize y = 0; y < image.height; y++)
            memcpy(dest + line_bytes*y, image_at(image, 0, (i32) y), line_bytes);
    }

    return ERROR_OK;
}

INTERNAL Error _netbpm_format_read_pgm_ppm(Image_Builder* image, String ppm, const char* magic, i32 channels)
{
    Error out_error = {0};
    String_Builder escaped_start = {0};
    array_init_backed(&escaped_start, allocator_get_scratch(), 128);
    String just_start = string_safe_head(ppm, escaped_start.capacity - 1);
    builder_assign(&escaped_start, just_start);
    
    char read_magic[3] = {0}; 
    int w = 0;
    int h = 0;
    int max_val = 0;
    int read_so_far = 0;
    int parsed = sscanf(escaped_start.data, "%2s\n%d %d\n%d\n%n", read_magic, &w, &h, &max_val, &read_so_far);

    if(parsed != 4)
        out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_INVALID_HEADER);
    else if(w <= 0 || h <= 0 || max_val <= 0 || max_val > 255 || strcmp(read_magic, magic) != 0) 
        out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_INVALID_HEADER_VALUES);
    else
    {
        isize pixel_count = w * h;
        isize needed_size = pixel_count * channels;
        if(ppm.size - needed_size - read_so_far < 0)
            out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_NOT_ENOUGH_DATA);
        else
        {
            Allocator* alloc = image->allocator;
            image_builder_init(image, alloc, channels, PIXEL_FORMAT_U8);
            image_builder_resize(image, w, h);
            memcpy(image->pixels, ppm.data, needed_size);
        }
    }

    array_deinit(&escaped_start);
    return out_error;
}

INTERNAL Error _netbpm_format_write_append_pfm_pfmg(String_Builder* append_into, Image image, const char* magic, i32 channels, f32 range)
{
    if(image.pixel_format != PIXEL_FORMAT_F32 && image_channel_count(image) != channels)
        return error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_BAD_TYPE);
        
    isize pixel_count = image.width * image.height;
    isize neeed_size = pixel_count * image.pixel_size;
    
    array_reserve(append_into, append_into->size + neeed_size + 40);

    f32 corrected_range = 0;
    if(endian_get_local() == ENDIAN_LITTLE)
        corrected_range = fabsf(range);
    else
        corrected_range = fabsf(range);

    format_append_into(append_into, "%s\n%d %d\n%d\n", magic, (int) image.width, (int) image.height, corrected_range);

    isize size_before = append_into->size;
    array_resize(append_into, size_before + neeed_size);

    u8* dest = (u8*) append_into->data + size_before;
    if(image_is_contiguous(image))
        memcpy(dest, image.pixels, neeed_size);
    else
    {
        isize line_bytes = image.width * image.pixel_size;
        for(isize y = 0; y < image.height; y++)
            memcpy(dest + line_bytes*y, image_at(image, 0, (i32) y), line_bytes);
    }

    return ERROR_OK;
}

INTERNAL Error _netbpm_format_read_pfm_pfmg(Image_Builder* image, String ppm, const char* magic, i32 channels)
{
    Error out_error = {0};
    String_Builder escaped_start = {0};
    array_init_backed(&escaped_start, allocator_get_scratch(), 128);
    String just_start = string_safe_head(ppm, escaped_start.capacity - 1);
    builder_assign(&escaped_start, just_start);

    char read_magic[3] = {0}; 
    int w = 0;
    int h = 0;
    f32 range = 0;
    int read_so_far = 0;
    int parsed = sscanf(escaped_start.data, "%2s\n%d %d\n%f\n%n", read_magic, &w, &h, &range, &read_so_far);

    if(parsed != 4)
        out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_INVALID_HEADER);
    else if(w <= 0 || h <= 0 || strcmp(read_magic, magic) != 0) 
        out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_INVALID_HEADER_VALUES);
    else
    {
        isize pixel_count = w * h;
        isize needed_size = pixel_count * channels * sizeof(f32);
        if(ppm.size - needed_size - read_so_far < 0)
            out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_NOT_ENOUGH_DATA);
        else
        {
            Allocator* alloc = image->allocator;
            image_builder_init(image, alloc, channels, PIXEL_FORMAT_F32);
            image_builder_resize(image, w, h);
            memcpy(image->pixels, ppm.data, needed_size);
        }
    }

    array_deinit(&escaped_start);
    return out_error;
}

EXPORT Error netbpm_format_pgm_write_into(String_Builder* into, Image image)
{
    array_clear(into);
    return _netbpm_format_write_append_pgm_ppm(into, image, "P5", 1);
}

EXPORT Error netbpm_format_ppm_write_into(String_Builder* into, Image image)
{
    array_clear(into);
    return _netbpm_format_write_append_pgm_ppm(into, image, "P6", 3);
}

EXPORT Error netbpm_format_pgm_read_into(Image_Builder* image, String ppm)
{
    return _netbpm_format_read_pgm_ppm(image, ppm, "P5", 1);
}

EXPORT Error netbpm_format_ppm_read_into(Image_Builder* image, String ppm)
{
    return _netbpm_format_read_pgm_ppm(image, ppm, "P6", 3);
}

EXPORT Error netbpm_format_pfmg_write_into(String_Builder* into, Image image, f32 range)
{
    array_clear(into);
    return _netbpm_format_write_append_pfm_pfmg(into, image, "Pf", 1, range);
}

EXPORT Error netbpm_format_pfm_write_into(String_Builder* into, Image image, f32 range)
{
    array_clear(into);
    return _netbpm_format_write_append_pfm_pfmg(into, image, "PF", 3, range);
}

EXPORT Error netbpm_format_pfmg_read_into(Image_Builder* image, String ppm)
{
    return _netbpm_format_read_pfm_pfmg(image, ppm, "Pf", 1);
}

EXPORT Error netbpm_format_pfm_read_into(Image_Builder* image, String ppm)
{
    return _netbpm_format_read_pfm_pfmg(image, ppm, "PF", 3);
}

EXPORT Error netbpm_format_pam_write_into(String_Builder* into, Image image)
{
    array_clear(into);
    String_Builder* append_into = into;

    isize channels = image_channel_count(image);
    isize neeed_size = image.width * image.height * image.pixel_size;
    
    array_reserve(append_into, append_into->size + neeed_size + 200);
    
    const char* tuple_type = NULL;
    int max_val = 0;
    int depth = 0;
    if(channels <= 4 && (image.pixel_format == PIXEL_FORMAT_U8 || image.pixel_format == PIXEL_FORMAT_U16))
    {
        max_val = image.pixel_format == PIXEL_FORMAT_U8 ? 255 : 65535;
        depth = (int) channels;
        switch(channels)
        {
            case 1: tuple_type = "GRAYSCALE"; break;
            case 2: tuple_type = "GRAYSCALE_ALPHA"; break;
            case 3: tuple_type = "RGB"; break;
            case 4: tuple_type = "RGB_ALPHA"; break;
            default: ASSERT(false); break;
        }
    }
    else if(image.pixel_format == PIXEL_FORMAT_F32)
    {   
        depth = image.pixel_size;
        tuple_type = "FLOATS";
        max_val = 255;
    }
    else if(image.pixel_format == PIXEL_FORMAT_U24)
    {
        depth = image.pixel_size;
        tuple_type = "U24";
        max_val = 255;
    }
    else if(image.pixel_format == PIXEL_FORMAT_U32)
    {
        depth = image.pixel_size;
        tuple_type = "U32";
        max_val = 255;
    }
    else
    {
        depth = image.pixel_size;
        tuple_type = "BYTES";
        max_val = 255;
    }

    format_append_into(append_into, 
        "P7\n"
        "WIDTH %d\n"
        "HEIGHT %d\n"
        "DEPTH %d\n"
        "MAXVAL %d\n"
        "TUPLTYPE %s\n"
        "ENDHDR", (int) image.width, (int) image.height, depth, max_val, tuple_type);

    isize size_before = append_into->size;
    array_resize(append_into, size_before + neeed_size);

    u8* dest = (u8*) append_into->data + size_before;
    if(image_is_contiguous(image))
        memcpy(dest, image.pixels, neeed_size);
    else
    {
        isize line_bytes = image.width * image.pixel_size;
        for(isize y = 0; y < image.height; y++)
            memcpy(dest + line_bytes*y, image_at(image, 0, (i32) y), line_bytes);
    }

    return ERROR_OK;
}

#include "parse.h"
EXPORT Error netbpm_format_pam_read_into(Image_Builder* image, String ppm)
{
    const Error INVALID_HEADER = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_INVALID_HEADER);
    isize file_pos = 0;
    if(match_sequence(ppm, &file_pos, STRING("P7\n")) == false)
        return INVALID_HEADER;

    isize width = -1;
    isize height = -1;
    isize maxval = -1;
    isize depth = -1;
    Image_Pixel_Format pixel_format = PIXEL_FORMAT_U8;

    Error out_error = {0};
    while(file_pos < ppm.size && error_is_ok(out_error))
    {
        isize line_end = string_find_first_char(ppm, '\n', file_pos);
        if(line_end == -1)
            line_end = ppm.size;

        String line = string_range(ppm, file_pos, line_end);
        u64 read = 0;
        isize line_index = 0;
        if(line_index = 0, 
            match_sequence(line, &line_index, STRING("#")))
        {
            //comment => do nothing
        }
        else if(line_index = 0, 
            match_sequence(line, &line_index, STRING("ENDHDR")))
        {
            break;
        }
        else if(line_index = 0, 
            match_sequence(line, &line_index, STRING("WIDTH")) 
            && match_whitespace(line, &line_index)
            && match_decimal_u64(line, &line_index, &read))
        {
            if(width != -1)
                out_error = INVALID_HEADER;

            width = read;
        }
        else if(line_index = 0, 
            match_sequence(line, &line_index, STRING("HEIGHT")) 
            && match_whitespace(line, &line_index)
            && match_decimal_u64(line, &line_index, &read))
        {
            if(height != -1)
                out_error = INVALID_HEADER;
            
            height = read;
        }
        else if(line_index = 0, 
            match_sequence(line, &line_index, STRING("DEPTH")) 
            && match_whitespace(line, &line_index)
            && match_decimal_u64(line, &line_index, &read))
        {
            if(depth != -1)
                out_error = INVALID_HEADER;
            if(read == 0)
                out_error = INVALID_HEADER;
            depth = read;
        }
        else if(line_index = 0, 
            match_sequence(line, &line_index, STRING("MAXVAL")) 
            && match_whitespace(line, &line_index)
            && match_decimal_u64(line, &line_index, &read))
        {
            if(maxval != -1)
                out_error = INVALID_HEADER;

            maxval = read;
        }
        else if(line_index = 0, 
            match_sequence(line, &line_index, STRING("TUPLTYPE")))
        {
            isize from = line_index;
            isize to = line.size;
            
            for(; from < line.size; from++)
                if(char_is_space(line.data[from]) == false)
                    break;

            for(; to-- > 0; )
                if(char_is_space(line.data[to]) == false)
                    break;

            String tuple_type = string_range(line, from, to);
            if(string_is_equal(tuple_type, STRING("FLOATS"))) 
                pixel_format = PIXEL_FORMAT_U8;
            else if(string_is_equal(tuple_type, STRING("U24"))) 
                pixel_format = PIXEL_FORMAT_U24;
            else if(string_is_equal(tuple_type, STRING("U32"))) 
                pixel_format = PIXEL_FORMAT_U32;
            else if(string_is_equal(tuple_type, STRING("BYTES"))) 
                pixel_format = PIXEL_FORMAT_U8;
        }
        else
        {
            out_error = INVALID_HEADER;
        }

        file_pos = line_end + 1;
    }

    if(width < 0 || height < 0 || maxval < 0 || depth <= 0)
        out_error = INVALID_HEADER;
    else
    {
        isize pixel_size = 1*depth;
        if(pixel_format == PIXEL_FORMAT_U8 && maxval > 255)
        {
            pixel_size = PIXEL_FORMAT_U16;
            pixel_size = 2*depth;
        }
        
        if(image_pixel_format_size(pixel_format) == 0 || pixel_size % image_pixel_format_size(pixel_format) != 0)
            pixel_format = PIXEL_FORMAT_U8;

        isize pixel_count = width * height;
        isize needed_size = pixel_count * pixel_size;

        String image_data = string_safe_tail(ppm, file_pos);
        if(image_data.size < needed_size)
            out_error = error_make(netbmp_format_error_module(), NETBPM_FORMAT_ERROR_NOT_ENOUGH_DATA);
        else
        {
            Allocator* alloc = image->allocator;
            image_builder_init_from_pixel_size(image, alloc, (i32) pixel_size, (Image_Pixel_Format) pixel_format);
            image_builder_resize(image, (i32) width, (i32) height);
            memcpy(image->pixels, ppm.data, needed_size);
        }
    }

    return out_error;

}

#endif
