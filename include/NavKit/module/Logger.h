#pragma once
#include <string>
#include "../../include/ConcurrentQueue/ConcurrentQueue.h"

enum LogCategory
{
    NK_INFO = 1,
    NK_WARN,
    NK_ERROR
};

class Logger {
    explicit Logger();

    rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>* logQueue;

public:
    static Logger &getInstance() {
        static Logger instance;
        return instance;
    }

    static void log(LogCategory category, const char *message, ...);

    static void logRunner();
};
