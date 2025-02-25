#include "../unicode.h"
#include "../utf.h"
#include "../random.h"
#include "../assert.h"

typedef enum {
    _UNI_LOWER,
    _UNI_UPPER,
    _UNI_TITLE,
    _UNI_SPACE,
    _UNI_DIGIT,
} _Unicode_Category;

void test_unicode_single(const char* str, _Unicode_Category cat)
{
    isize index = 0;
    uint32_t codepoint = (uint32_t) -1;
    while(utf8_decode(str, strlen(str), &codepoint, &index)) {
        TEST(unicode_is_lower(codepoint) == (cat == _UNI_LOWER));
        int k = 0; k++;
        TEST(unicode_is_upper(codepoint) == (cat == _UNI_UPPER));
        TEST(unicode_is_title(codepoint) == (cat == _UNI_TITLE));
        TEST(unicode_is_space(codepoint) == (cat == _UNI_SPACE));
        TEST(unicode_is_digit(codepoint) == (cat == _UNI_DIGIT));
        TEST(unicode_is_alpha(codepoint) == (cat == _UNI_LOWER || cat == _UNI_UPPER || cat == _UNI_TITLE));
    }
}

void test_unicode_unit()
{
    test_unicode_single("abcdefghijklmnopqrstuvwxyz", _UNI_LOWER);
    test_unicode_single("αβγδεζηθικλμνξοπρςστυφχψω", _UNI_LOWER);
    test_unicode_single("абвгґдеєжзиіїйклмнопрстуфхцчшщьюя", _UNI_LOWER);
    
    test_unicode_single("ABCDEFGHIJKLMNOPQRSTUVWXYZ", _UNI_UPPER);
    test_unicode_single("ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ", _UNI_UPPER);
    test_unicode_single("АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯ", _UNI_UPPER);
    
    test_unicode_single("    \t\v\f\n\r", _UNI_SPACE);
    test_unicode_single("0123456789߀", _UNI_DIGIT);
    test_unicode_single("໐໑໒໓໔໕໖໗໘໙໑໐໒໐", _UNI_DIGIT);
    test_unicode_single("߀߁߂߃߄߅߆߇߈߉", _UNI_DIGIT);
    test_unicode_single("٠١٢٣٤٥٦٧٨٩", _UNI_DIGIT);
    
    test_unicode_single("ǅǈǋᾈῼ", _UNI_TITLE);
}
