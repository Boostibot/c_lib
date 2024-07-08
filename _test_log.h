#pragma once

#include "_test.h"
#include "log.h"
#include "log_list.h"
#include "log_file.h"
#include "allocator_debug.h"

INTERNAL void test_log()
{
    LOG_INFO("TEST", "Ignore all logs below since they are a test!");
    log_group();

    Debug_Allocator debug_allocator = {0};
    debug_allocator_init_use(&debug_allocator, allocator_get_default(), DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK | DEBUG_ALLOCATOR_CAPTURE_CALLSTACK);

    {
        Log_List log_list = {0};
        log_list_init_capture(&log_list, &debug_allocator.allocator);

        LOG_INFO("TEST_LOG1", "%d", 25);
        LOG_INFO("TEST_LOG2", "hello");

        TEST(log_list.size == 2);
        TEST(string_is_equal(string_of(log_list.first->module), STRING("TEST_LOG1")));
        TEST(string_is_equal(log_list.first->message, STRING("25")));

        TEST(string_is_equal(string_of(log_list.first->next->module), STRING("TEST_LOG2")));
        TEST(string_is_equal(log_list.first->next->message, STRING("hello")));

        {
            File_Logger logger = {0};
            file_logger_init_use(&logger, &debug_allocator.allocator, "logs");
            LOG_TRACE("TEST_LOG", "trace %s", "?");
            LOG_DEBUG("TEST_LOG", "debug %s", "?");
            LOG_INFO("TEST_LOG", "info %s", ".");
            LOG_OKAY("TEST_LOG", "okay %s", ".");
            LOG_WARN("TEST_LOG", "warn %s", "!");
            LOG_ERROR("TEST_LOG", "error %s", "!");

            LOG_INFO("TEST_LOG", "iterating all entitites");

            for(int i = 0; i < 5; i++)
                LOG_INFO(">TEST_LOG", 
                    "entity id:%d found\n"
                    "Hello from entity\n\n\n", i);

            LOG_DEBUG("TEST_LOG", 
                "Debug info\n"
                "Some more info\n" 
                "%d-%d", 10, 20);
            
            file_logger_deinit(&logger);
        }

        log_list_deinit(&log_list);
    }
    debug_allocator_deinit(&debug_allocator);
    log_ungroup();
    
    LOG_INFO("TEST", "Tetsing log finished!");
}
