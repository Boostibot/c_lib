
static void test_string_find_single(const char* in_string_c, const char* search_for_c)
{
    String in_string = string_of(in_string_c);
    String search_for = string_of(search_for_c);

    for(isize from_i = 0; from_i <= in_string.count; from_i++)
    {
        const char* std_found = strstr(in_string_c + from_i, search_for_c);
        isize std_found_i = std_found ? std_found - in_string_c : -1;
        isize our_found_i = string_find_first(in_string, search_for, from_i);
        TEST(std_found_i == our_found_i);
        TEST(std_found_i == our_found_i);
    }
}

static void test_string_find()
{
    test_string_find_single("hello world", "hello");
    test_string_find_single("hello world", "world");
    test_string_find_single("hello world", "l");
    test_string_find_single("hello world", "orldw");
    test_string_find_single("hello world", "ll");
    test_string_find_single("world", "world world");
    test_string_find_single("wwwwwwww", "ww");
    test_string_find_single("abababaaa", "ba");
}