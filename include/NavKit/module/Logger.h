#pragma once
#include <string>
#include "../../include/ConcurrentQueue/ConcurrentQueue.h"

enum LogCategory {
    NK_INFO = 1,
    NK_WARN,
    NK_ERROR,
    NK_DEBUG
};

class Logger {
    explicit Logger();

    std::unique_ptr<rsj::ConcurrentQueue<std::pair<LogCategory, std::string> > > logQueue;

    bool debugLogsEnabled;

public:
    static Logger &getInstance() {
        static Logger instance;
        return instance;
    }

    static void log(LogCategory category, const char *format, ...);

    static void logRunner();
};
