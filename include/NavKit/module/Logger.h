#pragma once
#include <Recast.h>
#include <string>
#include "../../include/ConcurrentQueue/ConcurrentQueue.h"

enum LogCategory {
    NK_INFO = 1,
    NK_WARN,
    NK_ERROR,
    NK_DEBUG
};

class Logger {
    static const int MAX_MESSAGES = 250;
    const char* m_messages[MAX_MESSAGES];
    std::mutex m_log_mutex;
    std::deque<std::string> m_logBuffer;
    int m_messageCount;
    int m_textPoolSize;
    std::unique_ptr<rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>> logQueue;

public:
    int getLogCount() const;

    std::deque<std::string>& getLogBuffer();

    const char* getLogText(int i) const;

    void dumpLog(const char* format, ...);

    void doResetLog();

    void doLog(rcLogCategory category, const char* msg, int len);

    explicit Logger();

    bool debugLogsEnabled;

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    static void log(LogCategory category, const char* format, ...);

    [[noreturn]] static void logRunner();

    static void rustLogCallback(const char* message);

    std::mutex& getLogMutex();
};
