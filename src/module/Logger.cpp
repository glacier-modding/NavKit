#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"

#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/util/FileUtil.h"

Logger::Logger() :
    messageCount(0), textPoolSize(0),
    logQueue(std::make_unique<rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>>()), running(false) {
    memset(messages, 0, sizeof(char*) * MAX_MESSAGES);
}

Logger::~Logger() {
    stop();
}

void Logger::start() {
    if (logThread.joinable()) {
        return;
    }
#ifdef __APPLE__
    const std::filesystem::path logDirectory = FileUtil::getUserDataDirectory();
    std::error_code error;
    std::filesystem::create_directories(logDirectory, error);
    if (error) {
        std::cerr << "Could not create log directory " << logDirectory.string() << ": " << error.message() << '\n';
    }
    logFile.open(logDirectory / "NavKit.log", std::ios::out | std::ios::trunc);
#else
    logFile.open("NavKit.log", std::ios::out | std::ios::trunc);
#endif
    running = true;
    logThread = std::thread(logRunner);
}

void Logger::stop() {
    running = false;
    if (logThread.joinable()) {
        logThread.join();
    }
}

void Logger::doLog(const char* msg, const int len) {
    if (!len) {
        return;
    }

    std::lock_guard lock(logMutex);
    if (logBuffer.size() >= MAX_MESSAGES) {
        logBuffer.pop_front();
    }
    messageCount++;
    logBuffer.push_back(msg);
}

int Logger::getLogCount() const {
    return messageCount;
}

std::deque<std::string>& Logger::getLogBuffer() {
    return logBuffer;
}

void Logger::logRunner() {
    Logger& logger = getInstance();
    while (logger.running || !logger.logQueue->empty()) {
        if (std::optional<std::pair<LogCategory, std::string>> message = logger.logQueue->try_pop();
            message.has_value()) {
            std::string msg;
            switch (message.value().first) {
            case NK_ERROR:
                msg = "[ERROR] ";
                break;
            case NK_WARN:
                msg = "[WARN] ";
                break;
            case NK_DEBUG:
                if (!NavKitSettings::getInstance().showDebugLogs) {
                    continue;
                };
                msg = "[DEBUG] ";
                break;
            default:
                break;
            }
            msg += message.value().second;
            logger.logFile << msg << std::endl;
            logger.doLog(msg.c_str(), msg.length());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void Logger::rustLogCallback(const char* message) {
    std::string msg = message;
    if (!msg.empty() && msg.back() == '\n') {
        msg.pop_back();
    }
    log(NK_INFO, msg.c_str());
}

std::mutex& Logger::getLogMutex() {
    return logMutex;
}

void Logger::log(LogCategory category, const char* format, ...) {
    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);
    const int length = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (length < 0) {
        va_end(args);
        return;
    }

    std::vector<char> buffer(static_cast<size_t>(length) + 1);

    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    const std::pair logMessage = {category, std::string(buffer.data())};
    getInstance().logQueue->push(logMessage);
}
