#ifndef JOT_IMAGE
#define JOT_IMAGE

#include "allocator.h"
#include <limits.h>

//@TODO: flip_x, flip_y, rotate 90
//@TODO: set_chanels, convert

//some of the predefined pixel formats.
//Other custom formats can specified by using some positive number for Pixel_Type.
//That number is then the byte size of the data type. 
typedef enum Pixel_Type {
    PIXEL_TYPE_NONE = 0,

    PIXEL_TYPE_U8  = -1,
    PIXEL_TYPE_U16 = -2,
    PIXEL_TYPE_U24 = -3,
    PIXEL_TYPE_U32 = -4,
    PIXEL_TYPE_U64 = -8,

    PIXEL_TYPE_I8  = -11,
    PIXEL_TYPE_I16 = -12,
    PIXEL_TYPE_I24 = -13,
    PIXEL_TYPE_I32 = -14,
    PIXEL_TYPE_I64 = -18,
    
    PIXEL_TYPE_F8  = -21,
    PIXEL_TYPE_F16 = -22,
    PIXEL_TYPE_F32 = -24,
    PIXEL_TYPE_F64 = -28,

    //Any negative number not occupied by previous declarations
    // is considered invalid. This one is just a predefined constant 
    // guaranteed to remain invalid in the future.
    PIXEL_TYPE_INVALID = INT32_MIN
} Pixel_Type;

// A storage of 2D array of pixels holding the bare minimum to be usable. 
// Each pixel is pixel_size bytes long and there are width * height pixels.
// The type can be one of the Pixel_Type enum of negative values
// or it can be positive number of bytes per channel of the pixel.
// The number of channels can be calculated from pixel_size and type
// but is only secondary since most of the time we treat all channels 
// of a pixel as a unit.
typedef struct Image {
    Allocator* allocator;
    u8* pixels; 
    i32 pixel_size;
    Pixel_Type type; 

    i32 width;
    i32 height;

    isize capacity;
} Image;

// A non owning view into a subset of Image's data. 
// Has the same relatiionship to Image as String to String_Builder
typedef struct Subimage {
    u8* pixels;
    i32 pixel_size;
    i32 type;

    i32 containing_width;
    i32 containing_height;

    i32 from_x;
    i32 from_y;
    
    i32 width;
    i32 height;
} Subimage;

STATIC_ASSERT(sizeof(Image) <= 8*5);
STATIC_ASSERT(sizeof(Subimage) <= 8*5);

//returns the human readbale name of the pixel type. 
// Example reteturn values are u8, f32, i64, custom (pixel_type > 0) and invalid (pixel_type < 0 and none of the predefined)
EXPORT const char* pixel_type_name(Pixel_Type pixel_type);
//Returns the size of the pixel type. The return value is always bigger than 0.
EXPORT i32 pixel_type_size(Pixel_Type pixel_type);
EXPORT i32 pixel_type_size_or_zero(Pixel_Type pixel_type);
EXPORT i32 pixel_channel_count(Pixel_Type pixel_type, isize pixel_size);

EXPORT void image_init_unshaped(Image* image, Allocator* alloc);
EXPORT void image_init(Image* image, Allocator* alloc, isize pixel_size, Pixel_Type type);
//Initializes image with the given shape. If dat_or_null is not NULL fills it with data, otherwise fills it with 0.
EXPORT void image_init_sized(Image* image, Allocator* alloc, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null);
EXPORT void image_deinit(Image* image);
EXPORT void image_resize(Image* image, isize width, isize height);
EXPORT void image_reserve(Image* image, isize capacity);
EXPORT void image_copy(Image* to_image, Subimage from_image, isize offset_x, isize offset_y);
EXPORT void image_assign(Image* to_image, Subimage from_image);

//Gives the image the specified shape with width, height, channel_count and type. If the new shape is too big reallocates.
//Does not change the content within the image itself, likewise doesnt fill new size with 0 on size increase.
//If data_or_null is not NULL also copies the data into image.
EXPORT void image_reshape(Image* image, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null);

EXPORT void* image_at(Image image, isize x, isize y);
EXPORT Image image_from_image(Image to_copy, Allocator* alloc);
EXPORT Image image_from_subimage(Subimage to_copy, Allocator* alloc);

EXPORT Subimage image_portion(Image image, isize from_x, isize from_y, isize width, isize height);
EXPORT Subimage image_range(Image image, isize from_x, isize from_y, isize to_x, isize to_y);

EXPORT i32   image_channel_count(Image image);
EXPORT isize image_pixel_count(Image image);
EXPORT isize image_byte_stride(Image image);
EXPORT isize image_byte_size(Image image);

EXPORT Subimage subimage_of(Image image);
EXPORT Subimage subimage_make(void* pixels, isize width, isize height, isize pixel_size, Pixel_Type type);
EXPORT bool subimage_is_contiguous(Subimage view); //returns true if the view is contiguous in memory
EXPORT void* subimage_at(Subimage image, isize x, isize y);
EXPORT i32 subimage_channel_count(Subimage image);
EXPORT isize subimage_pixel_count(Subimage image);
EXPORT isize subimage_byte_stride(Subimage image);
EXPORT isize subimage_byte_size(Subimage image);

EXPORT Subimage subimage_portion(Subimage view, isize from_x, isize from_y, isize width, isize height);
EXPORT Subimage subimage_range(Subimage view, isize from_x, isize from_y, isize to_x, isize to_y);
EXPORT void subimage_copy(Subimage to_image, Subimage image, isize offset_x, isize offset_y);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_IMAGE_IMPL)) && !defined(JOT_IMAGE_HAS_IMPL)
#define JOT_IMAGE_HAS_IMPL

EXPORT const char* pixel_type_name(Pixel_Type pixel_type)
{
    switch(pixel_type)
    {
        case PIXEL_TYPE_NONE: return "none";
        case PIXEL_TYPE_U8: return "u8";
        case PIXEL_TYPE_U16: return "u16";
        case PIXEL_TYPE_U24: return "u24";
        case PIXEL_TYPE_U32: return "u32";
        case PIXEL_TYPE_U64: return "u64";
        
        case PIXEL_TYPE_I8: return "i8";
        case PIXEL_TYPE_I16: return "i16";
        case PIXEL_TYPE_I24: return "i24";
        case PIXEL_TYPE_I32: return "i32";
        case PIXEL_TYPE_I64: return "i64";

        case PIXEL_TYPE_F8: return "f8";
        case PIXEL_TYPE_F16: return "f16";
        case PIXEL_TYPE_F32: return "f32";
        case PIXEL_TYPE_F64: return "f64";

        case PIXEL_TYPE_INVALID: 
        default: 
        {
            if(pixel_type > 0)
                return "custom";
            else
                return "invalid";
        }
    }
}

EXPORT i32 pixel_type_size_or_zero(Pixel_Type pixel_type)
{
    switch(pixel_type)
    {
        case PIXEL_TYPE_NONE:  return 0;
        case PIXEL_TYPE_U8:  return 1;
        case PIXEL_TYPE_U16: return 2;
        case PIXEL_TYPE_U24: return 3;
        case PIXEL_TYPE_U32: return 4;
        case PIXEL_TYPE_U64: return 8;
        
        case PIXEL_TYPE_I8:  return 1;
        case PIXEL_TYPE_I16: return 2;
        case PIXEL_TYPE_I24: return 3;
        case PIXEL_TYPE_I32: return 4;
        case PIXEL_TYPE_I64: return 8;

        case PIXEL_TYPE_F8:  return 1;
        case PIXEL_TYPE_F16: return 2;
        case PIXEL_TYPE_F32: return 4;
        case PIXEL_TYPE_F64: return 8;
        
        case PIXEL_TYPE_INVALID: 
        default: {
            if(pixel_type > 0)
                return (i32) pixel_type;
            else
                return 0;
        }
    }
}

EXPORT i32 pixel_type_size(Pixel_Type pixel_type)
{
    return MAX(pixel_type_size_or_zero(pixel_type), 1);
}

EXPORT i32 pixel_channel_count(Pixel_Type pixel_type, isize pixel_size)
{
    i32 format_size = pixel_type_size(pixel_type);
    ASSERT(format_size > 0);
    i32 out = (i32) pixel_size / format_size;
    return out;
}

EXPORT i32 image_channel_count(Image image)
{
    return pixel_channel_count(image.type, image.pixel_size);
}

EXPORT isize image_pixel_count(Image image)
{
    return image.width * image.height;
}

EXPORT isize image_byte_stride(Image image)
{
    isize byte_stride = image.pixel_size * image.width;
    return byte_stride;
}

EXPORT isize image_byte_size(Image image)
{
    isize pixel_count = image_pixel_count(image);
    return image.pixel_size * pixel_count;
}


EXPORT void image_deinit(Image* image)
{
    allocator_deallocate(image->allocator, image->pixels, image->capacity, DEF_ALIGN);
    memset(image, 0, sizeof *image);
}

EXPORT void image_init(Image* image, Allocator* alloc, isize pixel_size, Pixel_Type type)
{
    image_deinit(image);
    image->allocator = alloc;
    image->pixel_size = (i32) pixel_size;
    image->type = (i32) type;
}

EXPORT void image_init_unshaped(Image* image, Allocator* alloc)
{
    image_deinit(image);
    image->allocator = alloc;
}

EXPORT void image_init_sized(Image* image, Allocator* alloc, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null)
{
    image_deinit(image);
    image->allocator = alloc;
    image_reshape(image, width, height, pixel_size, type, data_or_null);
    if(data_or_null == NULL)
        memset(image->pixels, 0, (size_t) image->capacity);
}

EXPORT void* image_at(Image image, isize x, isize y)
{
    CHECK_BOUNDS(x, image.width);
    CHECK_BOUNDS(y, image.height);

    isize byte_stride = image_byte_stride(image);
    u8* pixel = image.pixels + x*image.pixel_size + y*byte_stride;

    return pixel;
}

EXPORT i32 subimage_channel_count(Subimage image)
{
    return pixel_channel_count(image.type, image.pixel_size);
}

EXPORT isize subimage_byte_stride(Subimage image)
{
    isize stride = image.containing_width * image.pixel_size;

    return stride;
}

EXPORT isize subimage_pixel_count(Subimage image)
{
    return image.width * image.height;
}

EXPORT isize subimage_byte_size(Subimage image)
{
    isize pixel_count = subimage_pixel_count(image);
    return image.pixel_size * pixel_count;
}

EXPORT Subimage subimage_make(void* pixels, isize width, isize height, isize pixel_size, Pixel_Type type)
{
    Subimage view = {0};
    view.pixels = (u8*) pixels;
    view.pixel_size = (i32) pixel_size;
    view.type = (i32) type;

    view.containing_width = (i32) width;
    view.containing_height = (i32) height;

    view.from_x = 0;
    view.from_y = 0;
    view.width = (i32) width;
    view.height = (i32) height;
    return view;
}

EXPORT Subimage subimage_of(Image image)
{
    return subimage_make(image.pixels, image.width, image.height, image.pixel_size, image.type);
}

EXPORT bool subimage_is_contiguous(Subimage view)
{
    return view.from_x == 0 && view.width == view.containing_width;
}

EXPORT Subimage subimage_range(Subimage view, isize from_x, isize from_y, isize to_x, isize to_y)
{
    Subimage out = view;
    CHECK_BOUNDS(from_x, out.width + 1);
    CHECK_BOUNDS(from_y, out.height + 1);
    CHECK_BOUNDS(to_x, out.width + 1);
    CHECK_BOUNDS(to_y, out.height + 1);

    ASSERT(from_x <= to_x);
    ASSERT(from_y <= to_y);

    out.from_x = (i32) from_x;
    out.from_y = (i32) from_y;
    out.width = (i32) (to_x - from_x);
    out.height = (i32) (to_y - from_y);

    return out;
}

EXPORT Subimage subimage_portion(Subimage view, isize from_x, isize from_y, isize width, isize height)
{
    return subimage_range(view, from_x, from_y, from_x + width, from_y + height);
}

EXPORT Subimage image_portion(Image image, isize from_x, isize from_y, isize width, isize height)
{   
    return subimage_range(subimage_of(image), from_x, from_y, from_x + width, from_y + height);
}
EXPORT Subimage image_range(Image image, isize from_x, isize from_y, isize to_x, isize to_y)
{
    return subimage_range(subimage_of(image), from_x, from_y, to_x, to_y);
}

EXPORT void* subimage_at(Subimage view, isize x, isize y)
{
    CHECK_BOUNDS(x, view.width);
    CHECK_BOUNDS(y, view.height);

    i32 containing_x = (i32) x + view.from_x;
    i32 containing_y = (i32) y + view.from_y;
    
    isize byte_stride = subimage_byte_stride(view);

    u8* data = (u8*) view.pixels;
    isize offset = containing_x*view.pixel_size + containing_y*byte_stride;
    u8* pixel = data + offset;

    return pixel;
}

EXPORT void subimage_copy(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y)
{
    //Simple implementation
    i32 copy_width = from_image.width;
    i32 copy_height = from_image.height;
    if(copy_width == 0 || copy_height == 0)
        return;

    Subimage to_portion = subimage_portion(to_image, offset_x, offset_y, copy_width, copy_height);
    ASSERT(from_image.type == to_image.type, "formats must match!");
    ASSERT(from_image.pixel_size == to_image.pixel_size, "formats must match!");

    isize to_image_stride = subimage_byte_stride(to_image); 
    isize from_image_stride = subimage_byte_stride(from_image); 
    isize row_byte_size = copy_width * from_image.pixel_size;

    u8* to_image_ptr = (u8*) subimage_at(to_portion, 0, 0);
    u8* from_image_ptr = (u8*) subimage_at(from_image, 0, 0);

    //Copy in the right order so we dont override any data
    if(from_image_ptr >= to_image_ptr)
    {
        for(isize y = 0; y < copy_height; y++)
        { 
            memmove(to_image_ptr, from_image_ptr, (size_t) row_byte_size);

            to_image_ptr += to_image_stride;
            from_image_ptr += from_image_stride;
        }
    }
    else
    {
        //Reverse order copy
        to_image_ptr += copy_height*to_image_stride;
        from_image_ptr += copy_height*from_image_stride;

        for(isize y = 0; y < copy_height; y++)
        { 
            to_image_ptr -= to_image_stride;
            from_image_ptr -= from_image_stride;

            memmove(to_image_ptr, from_image_ptr, (size_t) row_byte_size);
        }
    }
}

EXPORT Image image_from_subimage(Subimage view, Allocator* alloc)
{
    Image image = {0};
    image_init_unshaped(&image, alloc);
    image_reshape(&image, view.width, view.height, view.pixel_size, view.type, NULL);

    image_copy(&image, view, 0, 0);
    return image;
}

EXPORT Image image_from_image(Image to_copy, Allocator* alloc)
{
    Image image = {0};
    image.allocator = alloc;
    image_assign(&image, subimage_of(to_copy));
    return image;
}

EXPORT void image_reserve(Image* image, isize capacity)
{
    if(capacity > image->capacity)
    {
        if(image->allocator == NULL)
            image->allocator = allocator_get_default();

        isize old_byte_size = image_byte_size(*image);
        u8* new_pixels = (u8*) allocator_allocate(image->allocator, capacity, DEF_ALIGN);
        
        memcpy(new_pixels, image->pixels, (size_t) old_byte_size);
        allocator_deallocate(image->allocator, image->pixels, image->capacity, DEF_ALIGN);

        image->pixels = new_pixels;
        image->capacity = capacity;
    }
}

EXPORT void image_reshape(Image* image, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null)
{
    isize needed_size = width*height*pixel_size;
    if(needed_size > image->capacity)
    {
        if(image->allocator == NULL)
            image->allocator = allocator_get_default();

        u8* new_pixels = (u8*) allocator_allocate(image->allocator, needed_size, DEF_ALIGN);
        allocator_deallocate(image->allocator, image->pixels, image->capacity, DEF_ALIGN);

        image->pixels = new_pixels;
        image->capacity = needed_size;
    }

    if(data_or_null)
    {
        ASSERT(image->capacity >= needed_size);   
        memmove(image->pixels, data_or_null, (size_t) needed_size);
    }

    image->width = (i32) width;
    image->height = (i32) height;
    image->pixel_size = (i32) pixel_size;
    image->type = type;
}

EXPORT void image_assign(Image* to_image, Subimage from_image)
{
    image_reshape(to_image, from_image.width, from_image.height, from_image.pixel_size, from_image.type, from_image.pixels);
    image_copy(to_image, from_image, 0, 0);
}

EXPORT void image_resize(Image* image, isize width, isize height)
{
    ASSERT(image != NULL && width >= 0 && height >= 0);
    
    if(image->width == width && image->height == height)
        return;

    if(image->allocator == NULL)
        image->allocator = allocator_get_default();

    if(image->pixel_size == 0)
    {
        ASSERT(image->width == 0 && image->height == 0);
        image->pixel_size = pixel_type_size(image->type);
    }

    isize new_byte_size = width * height * (isize) image->pixel_size;

    Image new_image = *image;
    new_image.width = (i32) width;
    new_image.height = (i32) height;
    if(new_byte_size > image->capacity)
    {
        new_image.pixels = (u8*) allocator_allocate(image->allocator, new_byte_size, DEF_ALIGN);
        new_image.capacity = new_byte_size;
        memset(new_image.pixels, 0, (size_t) new_byte_size);
    }

    Subimage to_view = subimage_of(new_image);
    Subimage from_view = subimage_of(*image);
    from_view.width = MIN(from_view.width, to_view.width);
    from_view.height = MIN(from_view.height, to_view.height);

    subimage_copy(to_view, from_view, 0, 0);

    if(new_byte_size > image->capacity)
        allocator_deallocate(image->allocator, image->pixels, image->capacity, DEF_ALIGN);

    *image = new_image;
}

EXPORT void image_copy(Image* to_image, Subimage from_image, isize offset_x, isize offset_y)
{
    subimage_copy(subimage_of(*to_image), from_image, offset_x, offset_y);
}

#endif