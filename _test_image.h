#pragma once

#include "image.h"
//#include "_test.h"

INTERNAL void test_image_builder_copy()
{
    Image_Builder from_image  = {0};
    image_builder_init(&from_image, allocator_get_default(), 1, PIXEL_FORMAT_U16);
    image_builder_resize(&from_image, 4, 4);

    for(isize x = 0; x < 4; x++)
        for(isize y = 0; y < 4; y++)
            *(u16*) image_builder_at(from_image, x, y) = x + y*4;

    Image_Builder to_image = {0};
    image_builder_init(&from_image, allocator_get_default(), 1, PIXEL_FORMAT_U16);
    image_builder_resize(&to_image, 2, 2);

    Image from_imagev = image_portion(image_from_builder(from_image), 1, 1, 2, 2);
    Image to_imagev = image_from_builder(to_image);

    image_copy(&to_imagev, from_imagev, 0, 0);
    TEST(*(u16*) image_builder_at(to_image, 0, 0) == 5);
    TEST(*(u16*) image_builder_at(to_image, 1, 0) == 6);
    TEST(*(u16*) image_builder_at(to_image, 0, 1) == 9);
    TEST(*(u16*) image_builder_at(to_image, 1, 1) == 10);
}

INTERNAL void test_image()
{
    test_image_builder_copy();
}