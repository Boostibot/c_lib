#pragma once

#include "log.h"
#include "allocator_debug.h"

INTERNAL void test_log()
{
    LOG_INFO("TEST", "Ignore all logs below since they are a test!");
    
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_USE);

    LOG_INFO("TEST_LOG1", "%d", 25);
    LOG_INFO("TEST_LOG2", "hello");

    {
        File_Logger logger = {0};
        file_logger_init(&logger, "logs", FILE_LOGGER_USE);
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
    debug_allocator_deinit(&debug_alloc);
    
    LOG_INFO("TEST", "Tetsing log finished!");
}
