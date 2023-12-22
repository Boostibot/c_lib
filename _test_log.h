#pragma once

#include "_test.h"
#include "log.h"
#include "logger_memory.h"
#include "logger_file.h"
#include "allocator_debug.h"

INTERNAL void test_log()
{

    Debug_Allocator debug_allocator = {0};
    debug_allocator_init_use(&debug_allocator, allocator_get_default(), DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK);

    {
        Memory_Logger mem_logger = {0};
        memory_logger_init_use(&mem_logger, &debug_allocator.allocator);

        LOG_INFO("TEST_LOG1", "%d", 25);
        LOG_INFO("TEST_LOG2", "hello");

        TEST(mem_logger.logs.size == 2);
        TEST(string_is_equal(memory_log_get_module(&mem_logger.logs.data[0]), STRING("TEST_LOG1")));
        TEST(string_is_equal(memory_log_get_message(&mem_logger.logs.data[0]), STRING("25")));

        TEST(string_is_equal(memory_log_get_module(&mem_logger.logs.data[1]), STRING("TEST_LOG2")));
        TEST(string_is_equal(memory_log_get_message(&mem_logger.logs.data[1]), STRING("hello")));

        {
            File_Logger logger = {0};
            file_logger_init_use(&logger, &debug_allocator.allocator, &debug_allocator.allocator);
            LOG_INFO("TEST_LOG", "iterating all entitites");

            log_group_push();
            for(int i = 0; i < 5; i++)
                LOG_INFO("TEST_LOG", 
                    "entity id:%d found\n"
                    "Hello from entity", i);
            log_group_pop();

            LOG_FATAL("TEST_LOG", 
                "Fatal error encountered!\n"
                "Some more info\n" 
                "%d-%d", 10, 20);
            
            file_logger_deinit(&logger);
        }
        memory_logger_deinit(&mem_logger);
    }
    debug_allocator_deinit(&debug_allocator);

}
