
#include "../platform.h"
#include "../assert.h"
#include <stdlib.h>

static void _platform_test_report(Platform_Error error, bool okay, const char* expression, const char* file, const char* function, int line, const char* format, ...)
{
    if((error == 0) != okay)
    {
        char err_msg[256] = {0};
        platform_translate_error(error, err_msg, sizeof err_msg);

        char user_error[256] = {0};
        va_list args;
        va_start(args, format);
        vsnprintf(user_error, sizeof user_error, format, args);
        va_end(args);

        if(okay)
            panic("TEST", expression, file, function, line, "expected no error but got error '%s' with message '%s'", err_msg, user_error);
        else
            panic("TEST", expression, file, function, line, "expected error but operation succeeded with message '%s'", user_error);
    }
}

static Platform_String _platform_cstring(const char* cstr)
{
    Platform_String out = {cstr, cstr ? strlen(cstr) : 0};
    return out;
}

static bool _platform_string_eq(Platform_String a, Platform_String b)
{
    return a.count == b.count && memcmp(a.data, b.data, a.count) == 0;
}

//String containing few problematic sequences: BOM, non ascii, non single UTF16 representable chars, \r\n and \n newlines.
//It should still be read in and out exactly the same!
#define PLATFORM_TEST_DIR "__platform_file_test_directory__"
#define PUGLY_STR        "\xEF\xBB\xBF" "Hello world!\r\n ěščřžýáéň,\n Φφ,Χχ,Ψψ,Ωω,\r\n あいうえお"
#define PUGLY_STRING     _platform_cstring(PUGLY_STR)
#define PTEST(ok, error, ...)  _platform_test_report((error), (ok), #error, __FILE__, __func__, __LINE__, "" __VA_ARGS__)

static void platform_test_file_content_equality(Platform_String path, Platform_String content);

static void platform_test_file_io() 
{
    PTEST(true, platform_directory_create(_platform_cstring(PLATFORM_TEST_DIR), false));
    PTEST(false, platform_directory_create(_platform_cstring(PLATFORM_TEST_DIR), true), "Creating already created directory should fail when fail_if_already_exists = true\n");
    {
        Platform_File_Info dir_info = {0};
        PTEST(true, platform_file_info(_platform_cstring(PLATFORM_TEST_DIR), &dir_info));
        TEST(dir_info.type == PLATFORM_FILE_TYPE_DIRECTORY);
        TEST(dir_info.link_type == PLATFORM_LINK_TYPE_NOT_LINK);

        Platform_String test_file_content = _platform_cstring(PUGLY_STR PUGLY_STR);
        Platform_String write_file_path = _platform_cstring(PLATFORM_TEST_DIR "/write_file.txt");
        Platform_String read_file_path = _platform_cstring(PLATFORM_TEST_DIR "/read_file.txt");
        Platform_String move_file_path = _platform_cstring(PLATFORM_TEST_DIR "/move_file.txt");
        
        //Cleanup any possibly remaining files from previous (failed) tests
        PTEST(true, platform_file_remove(write_file_path, false));
        PTEST(true, platform_file_remove(read_file_path, false));
        PTEST(true, platform_file_remove(move_file_path, false));

        //Write two PUGLY_STRING's into the file and flush it (no closing though!)
        Platform_File write_file = {0};
        PTEST(true, platform_file_open(&write_file, write_file_path, PLATFORM_FILE_OPEN_WRITE | PLATFORM_FILE_OPEN_CREATE | PLATFORM_FILE_OPEN_REMOVE_CONTENT));
        TEST(write_file.handle);
        PTEST(true, platform_file_write(&write_file, PUGLY_STRING.data, PUGLY_STRING.count, 0));
        PTEST(true, platform_file_write(&write_file, PUGLY_STRING.data, PUGLY_STRING.count, PUGLY_STRING.count));
        PTEST(true, platform_file_flush(&write_file));
        
        platform_test_file_content_equality(write_file_path, test_file_content);

        //Copy the file 
        PTEST(true, platform_file_copy(read_file_path, write_file_path, false));
        platform_test_file_content_equality(read_file_path, test_file_content);
        PTEST(true, platform_file_close(&write_file));

        //Move the file
        PTEST(true, platform_file_move(move_file_path, write_file_path, false));
        TEST(platform_file_info(write_file_path, NULL) != 0, "Opening of the moved from file should fail since its no longer there!\n");
        platform_test_file_content_equality(move_file_path, test_file_content);

        //Trim the file and 
        PTEST(true, platform_file_resize(move_file_path, PUGLY_STRING.count));
        platform_test_file_content_equality(move_file_path, PUGLY_STRING);

        //Cleanup the directory so it can be deleted.
        PTEST(true, platform_file_remove(write_file_path, false)); //Just in case
        PTEST(true, platform_file_remove(read_file_path, true));
        PTEST(true, platform_file_remove(move_file_path, true));
    }
    PTEST(true, platform_directory_remove(_platform_cstring(PLATFORM_TEST_DIR), true));
    PTEST(false, platform_directory_remove(_platform_cstring(PLATFORM_TEST_DIR), true), "removing a missing directory should fail when fail_if_not_found = true\n");
}

static void platform_test_file_content_equality(Platform_String path, Platform_String content)
{
    //Check file info for correctness
    Platform_File_Info info = {0};

    PTEST(true, platform_file_info(path, &info)); 

    TEST(info.type == PLATFORM_FILE_TYPE_FILE);
    TEST(info.link_type == PLATFORM_LINK_TYPE_NOT_LINK);
    TEST(info.size == content.count);
    
    //Read the entire file and check content for equality
    isize buffer_size = info.size;
    void* buffer = malloc((size_t) buffer_size);
    PTEST(true, platform_file_read_entire(path, buffer, buffer_size));
    TEST(memcmp(buffer, content.data, (size_t) content.count) == 0, "Content must match! Content: \n'%.*s' \nExpected: \n'%.*s'\n",
        (int) content.count, (char*) buffer, (int) content.count, content.data
    );
    free(buffer);
}

typedef struct Platform_Test_Dir_Entry {
    const char* path;
    Platform_File_Type type;
} Platform_Test_Dir_Entry;

void platform_test_list_entries(const char* directory, Platform_Test_Dir_Entry* tests, isize count)
{
    Platform_Directory_Iter iter = {0};
    Platform_String dir_path = {directory, strlen(directory) };
    Platform_Error error = platform_directory_iter_init(&iter, dir_path);

    PTEST(tests != NULL, error);
    if(error == 0) {
    
        isize concat_cap = 64*1024;
        char* concat = (char*) malloc(concat_cap);

        isize found_count = 0;
        for(; platform_directory_iter_next(&iter); found_count++) {
            
            bool found = 0;
            for(isize i = 0; i < count; i++) {
                if(strcmp(iter.path.data, tests[i].path) == 0) {
                    Platform_File_Info info = {0};

                    snprintf(concat, concat_cap, "%s/%s", directory, iter.path.data);
                    Platform_Error info_error = platform_file_info(_platform_cstring(concat), &info);
                    TEST(info_error == 0 && info.type == tests[i].type);
                    found = true;
                    break;
                }   
            }

            TEST(found);
        }

        isize positive_tests_count = 0;
        for(isize i = 0; i < count; i++) 
            positive_tests_count += tests[i].type != PLATFORM_FILE_TYPE_NOT_FOUND;

        TEST(found_count == positive_tests_count);
        platform_directory_iter_deinit(&iter);
        free(concat);
    }

}

static void platform_test_directory_list() 
{
    #define TEST_DIR_LIST_DIR       "__platform_dir_list_test_directory__"
    #define TEST_DIR_DEEPER1        TEST_DIR_LIST_DIR "/deeper1"
    #define TEST_DIR_DEEPER2        TEST_DIR_LIST_DIR "/deeper2"
    #define TEST_DIR_DEEPER3        TEST_DIR_LIST_DIR "/deeper3"
    #define TEST_DIR_DEEPER3_INNER  TEST_DIR_DEEPER3 "/inner"

    PTEST(true, platform_directory_create(_platform_cstring(TEST_DIR_LIST_DIR), false));
    {
        PTEST(true, platform_directory_create(_platform_cstring(TEST_DIR_DEEPER1), false));
        PTEST(true, platform_directory_create(_platform_cstring(TEST_DIR_DEEPER2), false));
        PTEST(true, platform_directory_create(_platform_cstring(TEST_DIR_DEEPER3), false));
        PTEST(true, platform_directory_create(_platform_cstring(TEST_DIR_DEEPER3_INNER), false));

        Platform_String temp_file1 = _platform_cstring(TEST_DIR_LIST_DIR "/temp_file1.txt");
        Platform_String temp_file2 = _platform_cstring(TEST_DIR_LIST_DIR "/temp_file2.txt");
        Platform_String temp_file3 = _platform_cstring(TEST_DIR_LIST_DIR "/temp_file3.txt");
        Platform_String temp_file_deep1_1 = _platform_cstring(TEST_DIR_DEEPER1 "/temp_deeper1_file1.txt");
        Platform_String temp_file_deep1_2 = _platform_cstring(TEST_DIR_DEEPER1 "/temp_deeper1_file2.txt");
        Platform_String temp_file_deep3_1 = _platform_cstring(TEST_DIR_DEEPER3_INNER "/temp_deeper3_inner_file1.txt");
        Platform_String temp_file_deep3_2 = _platform_cstring(TEST_DIR_DEEPER3_INNER "/temp_deeper3_inner_file2.txt");

        Platform_File first = {0};
        PTEST(true, platform_file_open(&first, temp_file1, PLATFORM_FILE_OPEN_WRITE | PLATFORM_FILE_OPEN_CREATE | PLATFORM_FILE_OPEN_REMOVE_CONTENT));
        PTEST(true, platform_file_write(&first, PUGLY_STRING.data, PUGLY_STRING.count, 0));
        PTEST(true, platform_file_close(&first));

        PTEST(true, platform_file_copy(temp_file2, temp_file1, true));
        PTEST(true, platform_file_copy(temp_file3, temp_file1, true));

        PTEST(true, platform_file_copy(temp_file_deep1_1, temp_file1, true));
        PTEST(true, platform_file_copy(temp_file_deep1_2, temp_file1, true));
            
        PTEST(true, platform_file_copy(temp_file_deep3_1, temp_file1, true));
        PTEST(true, platform_file_copy(temp_file_deep3_2, temp_file1, true));

        //Now the dir should look like (inside PLATFORM_TEST_DIR):
        // PLATFORM_TEST_DIR:
        //    temp_file1.txt
        //    temp_file2.txt
        //    temp_file3.txt
        //    deeper1:
        //         temp_deeper1_file1.txt
        //         temp_deeper1_file2.txt
        //    deeper2:
        //    deeper3:
        //         inner:
        //             temp_deeper3_inner_file1.txt
        //             temp_deeper3_inner_file2.txt

        //test some nonexistant directories - must not open
        platform_test_list_entries(" ", NULL, 0);
        platform_test_list_entries("not_existant_1", NULL, 0);
        platform_test_list_entries("not_existant_2", NULL, 0);
        platform_test_list_entries("ýýčěýčéč", NULL, 0);

        {
            Platform_Test_Dir_Entry entries[] = {
                "temp_file1.txt", PLATFORM_FILE_TYPE_FILE,
                "temp_file2.txt", PLATFORM_FILE_TYPE_FILE,
                "temp_file3.txt", PLATFORM_FILE_TYPE_FILE,

                "deeper1", PLATFORM_FILE_TYPE_DIRECTORY,
                "deeper2", PLATFORM_FILE_TYPE_DIRECTORY,
                "deeper3", PLATFORM_FILE_TYPE_DIRECTORY,

                "temp_file3",       PLATFORM_FILE_TYPE_NOT_FOUND,
                "fakakjfgáýčěá.txt", PLATFORM_FILE_TYPE_NOT_FOUND,
                "temp_deeper1_file1", PLATFORM_FILE_TYPE_NOT_FOUND,
                "temp_deeper1_file2", PLATFORM_FILE_TYPE_NOT_FOUND,

                "deeper3/inner", PLATFORM_FILE_TYPE_NOT_FOUND,
            };

            platform_test_list_entries(TEST_DIR_LIST_DIR, entries, sizeof(entries)/sizeof(entries[0]));
        }
        
        {
            Platform_Test_Dir_Entry entries[] = {
                "temp_deeper1_file1.txt", PLATFORM_FILE_TYPE_FILE,
                "temp_deeper1_file2.txt", PLATFORM_FILE_TYPE_FILE,

                "temp_file1.txt", PLATFORM_FILE_TYPE_NOT_FOUND,
                "temp_file2.txt", PLATFORM_FILE_TYPE_NOT_FOUND,
                "temp_file3.txt", PLATFORM_FILE_TYPE_NOT_FOUND,

                "deeper1", PLATFORM_FILE_TYPE_NOT_FOUND,
                "deeper2", PLATFORM_FILE_TYPE_NOT_FOUND,
                "deeper3", PLATFORM_FILE_TYPE_NOT_FOUND,

                "temp_file3",       PLATFORM_FILE_TYPE_NOT_FOUND,
                "fakakjfgáýčěá.txt", PLATFORM_FILE_TYPE_NOT_FOUND,

                "deeper3/inner", PLATFORM_FILE_TYPE_NOT_FOUND,
            };

            platform_test_list_entries(TEST_DIR_LIST_DIR "/deeper1", entries, sizeof(entries)/sizeof(entries[0]));
        }
        
        {
            Platform_Test_Dir_Entry entries[] = {
                "temp_deeper3_inner_file1.txt", PLATFORM_FILE_TYPE_FILE,
                "temp_deeper3_inner_file2.txt", PLATFORM_FILE_TYPE_FILE,

                "temp_file1.txt", PLATFORM_FILE_TYPE_NOT_FOUND,
                "temp_file2.txt", PLATFORM_FILE_TYPE_NOT_FOUND,
                "temp_file3.txt", PLATFORM_FILE_TYPE_NOT_FOUND,

                "deeper1", PLATFORM_FILE_TYPE_NOT_FOUND,
                "deeper2", PLATFORM_FILE_TYPE_NOT_FOUND,
                "deeper3", PLATFORM_FILE_TYPE_NOT_FOUND,

                "temp_file3",       PLATFORM_FILE_TYPE_NOT_FOUND,
                "fakakjfgáýčěá.txt", PLATFORM_FILE_TYPE_NOT_FOUND,

                "deeper3/inner", PLATFORM_FILE_TYPE_NOT_FOUND,
            };

            platform_test_list_entries(TEST_DIR_LIST_DIR "/deeper3/inner", entries, sizeof(entries)/sizeof(entries[0]));
        }

        PTEST(true, platform_file_remove(temp_file1, true));
        PTEST(true, platform_file_remove(temp_file2, true));
        PTEST(true, platform_file_remove(temp_file3, true));

        PTEST(true, platform_file_remove(temp_file_deep1_1, true));
        PTEST(true, platform_file_remove(temp_file_deep1_2, true));
            
        PTEST(true, platform_file_remove(temp_file_deep3_1, true));
        PTEST(true, platform_file_remove(temp_file_deep3_2, true));

        PTEST(true, platform_directory_remove(_platform_cstring(TEST_DIR_DEEPER3_INNER), true));
        PTEST(true, platform_directory_remove(_platform_cstring(TEST_DIR_DEEPER1), true));
        PTEST(true, platform_directory_remove(_platform_cstring(TEST_DIR_DEEPER2), true));
        PTEST(true, platform_directory_remove(_platform_cstring(TEST_DIR_DEEPER3), true));
    }
    PTEST(true, platform_directory_remove(_platform_cstring(TEST_DIR_LIST_DIR), true));
}


const char* _platform_file_watch_flag_cstring(Platform_File_Watch_Flag flag)
{
    switch(flag) {
        case PLATFORM_FILE_WATCH_CREATED: return "PLATFORM_FILE_WATCH_CREATED"; 
        case PLATFORM_FILE_WATCH_DELETED: return "PLATFORM_FILE_WATCH_DELETED";
        case PLATFORM_FILE_WATCH_MODIFIED: return "PLATFORM_FILE_WATCH_MODIFIED";
        case PLATFORM_FILE_WATCH_RENAMED: return "PLATFORM_FILE_WATCH_RENAMED";
        case PLATFORM_FILE_WATCH_DIRECTORY: return "PLATFORM_FILE_WATCH_DIRECTORY";
        case PLATFORM_FILE_WATCH_OVERFLOW: return "PLATFORM_FILE_WATCH_OVERFLOW";
        default: return "unknown";
    };
}

static void platform_test_file_watch()
{
    Platform_String watched_path = _platform_cstring(PLATFORM_TEST_DIR);
    Platform_String content = _platform_cstring("hello world!");

    PTEST(true, platform_directory_create(_platform_cstring(PLATFORM_TEST_DIR), false));
    {
        Platform_File_Watch watch = {0};
        PTEST(true, platform_file_watch_init(&watch, PLATFORM_FILE_WATCH_ALL, _platform_cstring(PLATFORM_TEST_DIR), -1));

        PTEST(true, platform_file_create(_platform_cstring(PLATFORM_TEST_DIR "/create_file1.txt"), true));
        PTEST(true, platform_file_create(_platform_cstring(PLATFORM_TEST_DIR "/create_file2.txt"), true));
        PTEST(true, platform_file_create(_platform_cstring(PLATFORM_TEST_DIR "/create_file3.txt"), true));
        PTEST(true, platform_file_move(_platform_cstring(PLATFORM_TEST_DIR "/move_file1.txt"), _platform_cstring(PLATFORM_TEST_DIR "/create_file1.txt"), true));
        PTEST(true, platform_file_move(_platform_cstring(PLATFORM_TEST_DIR "/move_file2.txt"), _platform_cstring(PLATFORM_TEST_DIR "/move_file1.txt"), true));
    
        PTEST(true, platform_file_append_entire(_platform_cstring(PLATFORM_TEST_DIR "/create_file3.txt"), content.data, content.count, true));
        PTEST(true, platform_file_append_entire(_platform_cstring(PLATFORM_TEST_DIR "/create_file3.txt"), content.data, content.count, true));
        PTEST(true, platform_file_remove(_platform_cstring(PLATFORM_TEST_DIR "/move_file2.txt"), true));
        PTEST(true, platform_file_remove(_platform_cstring(PLATFORM_TEST_DIR "/create_file2.txt"), true));
        PTEST(true, platform_file_remove(_platform_cstring(PLATFORM_TEST_DIR "/create_file3.txt"), true));

        Platform_File_Watch_Event possible_events[] = {
            {0, PLATFORM_FILE_WATCH_CREATED, watched_path, _platform_cstring("create_file1.txt")},
            {0, PLATFORM_FILE_WATCH_CREATED, watched_path, _platform_cstring("create_file2.txt")},
            {0, PLATFORM_FILE_WATCH_CREATED, watched_path, _platform_cstring("create_file3.txt")},
            {0, PLATFORM_FILE_WATCH_RENAMED, watched_path, _platform_cstring("create_file1.txt"), _platform_cstring("move_file1.txt")},
            {0, PLATFORM_FILE_WATCH_RENAMED, watched_path, _platform_cstring("move_file1.txt"), _platform_cstring("move_file2.txt")},
            {0, PLATFORM_FILE_WATCH_MODIFIED, watched_path, _platform_cstring("create_file3.txt")},
            {0, PLATFORM_FILE_WATCH_DELETED, watched_path, _platform_cstring("move_file2.txt")},
            {0, PLATFORM_FILE_WATCH_DELETED, watched_path, _platform_cstring("create_file2.txt")},
            {0, PLATFORM_FILE_WATCH_DELETED, watched_path, _platform_cstring("create_file3.txt")},
        };

        for(Platform_File_Watch_Event event = {0}; platform_file_watch_poll(&watch, &event, NULL); ) 
        {
            for(isize i = 0; i < sizeof(possible_events)/sizeof(possible_events[0]); i++) {
                Platform_File_Watch_Event curr = possible_events[i];
                if(event.action == curr.action 
                    && _platform_string_eq(event.watched_path, curr.watched_path) 
                    && _platform_string_eq(event.path, curr.path) 
                    && _platform_string_eq(event.new_path, curr.new_path)) {
                    possible_events[i]._ = true;
                    break;
                } 
            }
        }
        
        for(isize i = 0; i < sizeof(possible_events)/sizeof(possible_events[0]); i++) 
            TEST(possible_events[i]._);
        
        platform_file_watch_deinit(&watch);
    }
    PTEST(true, platform_directory_remove(_platform_cstring(PLATFORM_TEST_DIR), true));
}

static void platform_test_all() 
{   
    printf("platform_test_all() running at directory: '%s'\n", platform_directory_get_startup_working());

    TEST(strlen(platform_directory_get_startup_working()) > 0);
    TEST(strlen(platform_get_executable_path()) > 0);

    platform_test_file_watch();
    platform_test_file_io();
    platform_test_directory_list();
}

