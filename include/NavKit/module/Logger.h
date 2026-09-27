#pragma once
#include <atomic>
#include <string>
#include <thread>
#include "../../ConcurrentQueue/ConcurrentQueue.h"
#include <fstream>

enum LogCategory { NK_INFO = 1, NK_WARN, NK_ERROR, NK_DEBUG };

class Logger {
    static const int MAX_MESSAGES = 250;
    const char* messages[MAX_MESSAGES];
    std::mutex logMutex;
    std::ofstream logFile;
    std::deque<std::string> logBuffer;
    int messageCount;
    int textPoolSize;
    std::unique_ptr<rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>> logQueue;
    std::atomic<bool> running;
    std::thread logThread;

    static void logRunner();

public:
    explicit Logger();

    ~Logger();

    int getLogCount() const;

    std::deque<std::string>& getLogBuffer();

    void doLog(const char* msg, int len);

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    static void log(LogCategory category, const char* format, ...);

    void start();

    void stop();

    static void rustLogCallback(const char* message);

    std::mutex& getLogMutex();
};
