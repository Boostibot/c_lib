#ifndef LIB_IMAGE
#define LIB_IMAGE

#include "allocator.h"
#include <limits.h>

//@TODO: flip_x flip_y and remove those from the load image 
//@TODO: set_chanels, convert

//some of the predefined pixel formats.
//Other unspecified formats can specified by using some
//positive number for its format. 
//That number is then the byte size of the data type.
typedef enum Image_Pixel_Format {
    PIXEL_FORMAT_U8 = -1,
    PIXEL_FORMAT_U16 = -2,
    PIXEL_FORMAT_U24 = -3,
    PIXEL_FORMAT_U32 = -4,
    PIXEL_FORMAT_F32 = -5,
} Image_Pixel_Format;

// A storage of 2D array of pixels holding the bare minimum to be usable. 
// Each pixel is pixel_size bytes long and there are width * height pixels.
// The pixel_format can be one of the Image_Pixel_Format enum of negative values
// or it can be positive number of bytes per channel of the pixel.
// The number of channels can be calculated from pixel_size and pixel_format
// but is only second class since most of the time we treat all channels 
// of a pixel as a unit.
typedef struct Image_Builder
{
    Allocator* allocator;
    u8* pixels; 
    i32 pixel_size;
    i32 pixel_format; 

    i32 width;
    i32 height;
} Image_Builder;

// A non owning view into a subset of Image_Builder's data. 
// Has the same relatiionship to Image_Builder as String to String_Builder
typedef struct Image {
    u8* pixels;
    i32 pixel_size;
    i32 pixel_format;

    i32 containing_width;
    i32 containing_height;

    i32 from_x;
    i32 from_y;
    
    i32 width;
    i32 height;
} Image;

STATIC_ASSERT(sizeof(Image_Builder) <= 8*4);
STATIC_ASSERT(sizeof(Image) <= 8*5);

EXPORT isize image_pixel_format_size(Image_Pixel_Format format);

EXPORT void image_builder_init(Image_Builder* image, Allocator* alloc, i32 channel_count, Image_Pixel_Format pixel_format);
EXPORT void image_builder_init_from_pixel_size(Image_Builder* image, Allocator* alloc, i32 pixel_size, Image_Pixel_Format pixel_format);
EXPORT void image_builder_init_from_image(Image_Builder* image, Allocator* alloc, Image view);
EXPORT void image_builder_deinit(Image_Builder* image);
EXPORT void image_builder_resize(Image_Builder* image, i32 width, i32 height);
EXPORT void image_builder_copy(Image_Builder* to_image, Image from_image, i32 offset_x, i32 offset_y);
EXPORT void* image_builder_at(Image_Builder image, i32 x, i32 y);

EXPORT isize image_builder_channel_count(Image_Builder image);
EXPORT isize image_builder_pixel_count(Image_Builder image);
EXPORT isize image_builder_byte_stride(Image_Builder image);
EXPORT isize image_builder_all_pixels_size(Image_Builder image);

EXPORT Image image_from_builder(Image_Builder image);
EXPORT Image image_from_data(void* pixels, isize width, isize height, isize pixel_size, Image_Pixel_Format pixel_format);
EXPORT bool image_is_contiguous(Image view); //returns true if the view is contiguous in memory
EXPORT void* image_at(Image image, i32 x, i32 y);
EXPORT isize image_channel_count(Image image);
EXPORT isize image_pixel_count(Image image);
EXPORT isize image_byte_stride(Image image);

EXPORT Image image_portion(Image view, i32 from_x, i32 from_y, i32 width, i32 height);
EXPORT Image image_range(Image view, i32 from_x, i32 from_y, i32 to_x, i32 to_y);
EXPORT void image_copy(Image* to_image, Image image, i32 offset_x, i32 offset_y);

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_IMAGE_IMPL)) && !defined(LIB_IMAGE_HAS_IMPL)
#define LIB_IMAGE_HAS_IMPL

EXPORT isize image_pixel_format_size(Image_Pixel_Format format)
{
    switch(format)
    {
        default:                return abs((int) format);
        case PIXEL_FORMAT_U8:   return 1;
        case PIXEL_FORMAT_U16:  return 2;
        case PIXEL_FORMAT_U24:  return 3;
        case PIXEL_FORMAT_U32:  return 4;
        case PIXEL_FORMAT_F32:  return 4;
    }
}

EXPORT isize image_builder_channel_count(Image_Builder image)
{
    isize format_size = MAX(image_pixel_format_size((Image_Pixel_Format) image.pixel_format), 1);
    isize out = image.pixel_size / format_size;
    return out;
}

EXPORT isize image_builder_pixel_count(Image_Builder image)
{
    return image.width * image.height;
}

EXPORT isize image_builder_byte_stride(Image_Builder image)
{
    isize byte_stride = image.pixel_size * image.width;
    return byte_stride;
}

EXPORT isize image_builder_all_pixels_size(Image_Builder image)
{
    isize pixel_count = image_builder_pixel_count(image);
    return image.pixel_size * pixel_count;
}


EXPORT void image_builder_deinit(Image_Builder* image)
{
    isize total_size = image_builder_all_pixels_size(*image);
    allocator_deallocate(image->allocator, image->pixels, total_size, DEF_ALIGN, SOURCE_INFO());
    memset(image, 0, sizeof *image);
}


EXPORT void image_builder_init_from_pixel_size(Image_Builder* image, Allocator* alloc, i32 pixel_size, Image_Pixel_Format pixel_format)
{
    image_builder_deinit(image);
    image->allocator = alloc;
    image->pixel_size = (i32) pixel_size;
    image->pixel_format = (i32) pixel_format;
}

EXPORT void image_builder_init(Image_Builder* image, Allocator* alloc, i32 channel_count, Image_Pixel_Format pixel_format)
{
    isize pixel_format_size = image_pixel_format_size(pixel_format);
    isize pixel_size = (isize) channel_count * pixel_format_size;

    image_builder_deinit(image);
    image->allocator = alloc;
    image->pixel_size = (i32) pixel_size;
    image->pixel_format = (i32) pixel_format;
}

EXPORT void* image_builder_at(Image_Builder image, i32 x, i32 y)
{
    CHECK_BOUNDS(x, image.width);
    CHECK_BOUNDS(y, image.height);

    isize byte_stride = image_builder_byte_stride(image);
    u8* pixel = image.pixels + x*image.pixel_size + y*byte_stride;

    return pixel;
}

EXPORT isize image_channel_count(Image image)
{
    isize format_size = MAX(image_pixel_format_size((Image_Pixel_Format) image.pixel_format), 1);
    isize out = image.pixel_size / format_size;
    return out;
}

EXPORT isize image_byte_stride(Image image)
{
    isize stride = image.containing_width * image.pixel_size;

    return stride;
}

EXPORT isize image_pixel_count(Image image)
{
    return image.width * image.height;
}

EXPORT Image image_from_data(void* pixels, isize width, isize height, isize pixel_size, Image_Pixel_Format pixel_format)
{
    Image view = {0};
    view.pixels = (u8*) pixels;
    view.pixel_size = (i32) pixel_size;
    view.pixel_format = (i32) pixel_format;

    view.containing_width = (i32) width;
    view.containing_height = (i32) height;

    view.from_x = 0;
    view.from_y = 0;
    view.width = (i32) width;
    view.height = (i32) height;
    return view;
}

EXPORT Image image_from_builder(Image_Builder image)
{
    return image_from_data(image.pixels, image.width, image.height, image.pixel_size, (Image_Pixel_Format) image.pixel_format);
}

EXPORT bool image_is_contiguous(Image view)
{
    return view.from_x == 0 && view.width == view.containing_width;
}

EXPORT Image image_range(Image view, i32 from_x, i32 from_y, i32 to_x, i32 to_y)
{
    Image out = view;
    CHECK_BOUNDS(from_x, out.width + 1);
    CHECK_BOUNDS(from_y, out.height + 1);
    CHECK_BOUNDS(to_x, out.width + 1);
    CHECK_BOUNDS(to_y, out.height + 1);

    ASSERT(from_x <= to_x);
    ASSERT(from_y <= to_y);

    out.from_x = from_x;
    out.from_y = from_y;
    out.width = to_x - from_x;
    out.height = to_y - from_y;

    return out;
}

EXPORT Image image_portion(Image view, i32 from_x, i32 from_y, i32 width, i32 height)
{
    Image out = image_range(view, from_x, from_y, from_x + width, from_y + height);
    return out;
}

EXPORT void* image_at(Image view, i32 x, i32 y)
{
    CHECK_BOUNDS(x, view.width);
    CHECK_BOUNDS(y, view.height);

    i32 containing_x = x + view.from_x;
    i32 containing_y = y + view.from_y;
    
    isize byte_stride = image_byte_stride(view);

    u8* data = (u8*) view.pixels;
    isize offset = containing_x*view.pixel_size + containing_y*byte_stride;
    u8* pixel = data + offset;

    return pixel;
}

EXPORT void image_copy(Image* to_image, Image from_image, i32 offset_x, i32 offset_y)
{
    //Simple implementation
    i32 copy_width = from_image.width;
    i32 copy_height = from_image.height;
    if(copy_width == 0 || copy_height == 0)
        return;

    Image to_portion = image_portion(*to_image, offset_x, offset_y, copy_width, copy_height);
    ASSERT_MSG(from_image.pixel_format == to_image->pixel_format, "formats must match!");
    ASSERT_MSG(from_image.pixel_size == to_image->pixel_size, "formats must match!");

    isize to_image_stride = image_byte_stride(*to_image); 
    isize from_image_stride = image_byte_stride(from_image); 
    isize row_byte_size = copy_width * from_image.pixel_size;

    u8* to_image_ptr = (u8*) image_at(to_portion, 0, 0);
    u8* from_image_ptr = (u8*) image_at(from_image, 0, 0);

    //Copy in the right order so we dont override any data
    if(from_image_ptr >= to_image_ptr)
    {
        for(isize y = 0; y < copy_height; y++)
        { 
            memmove(to_image_ptr, from_image_ptr, row_byte_size);

            to_image_ptr += to_image_stride;
            from_image_ptr += from_image_stride;
        }
    }
    else
    {
        //Reverse order copy
        to_image_ptr += to_image_stride + copy_height*to_image_stride;
        from_image_ptr += from_image_stride + copy_height*from_image_stride;

        for(isize y = 0; y < copy_height; y++)
        { 
            to_image_ptr -= to_image_stride;
            from_image_ptr -= from_image_stride;

            memmove(to_image_ptr, from_image_ptr, row_byte_size);
        }
    }
}

EXPORT void image_builder_init_from_image(Image_Builder* image, Allocator* alloc, Image view)
{
    isize new_byte_size = (isize) view.width * (isize) view.height * (isize) view.pixel_size;

    image_builder_deinit(image);
    image_builder_init_from_pixel_size(image, alloc, view.pixel_size, (Image_Pixel_Format) view.pixel_format);
    image->width = view.width;
    image->height = view.height;
    image->pixels = (u8*) allocator_allocate(image->allocator, new_byte_size, DEF_ALIGN, SOURCE_INFO());
    image_builder_copy(image, view, 0, 0);
}

EXPORT void image_builder_resize(Image_Builder* image, i32 width, i32 height)
{
    ASSERT(image != NULL && width >= 0 && height >= 0);

    isize old_byte_size = image_builder_all_pixels_size(*image);
    isize new_byte_size = (isize) width * height * image->pixel_size;
    
    if(image->allocator == NULL)
        image->allocator = allocator_get_default();

    Image_Builder new_image = {0};
    new_image.allocator = image->allocator;
    new_image.width = width;
    new_image.height = height;
    new_image.pixel_format = image->pixel_format;
    new_image.pixel_size = image->pixel_size;
    new_image.pixels = (u8*) allocator_allocate(image->allocator, new_byte_size, DEF_ALIGN, SOURCE_INFO());
    memset(new_image.pixels, 0, new_byte_size);

    Image to_view = image_from_builder(new_image);
    Image from_view = image_from_builder(*image);

    image_copy(&to_view, from_view, 0, 0);

    allocator_deallocate(image->allocator, image->pixels, old_byte_size, DEF_ALIGN, SOURCE_INFO());

    *image = new_image;
}

EXPORT void image_builder_copy(Image_Builder* to_image, Image from_image, i32 offset_x, i32 offset_y)
{
    Image to_view = image_from_builder(*to_image);
    image_copy(&to_view, from_image, offset_x, offset_y);
}

#endif