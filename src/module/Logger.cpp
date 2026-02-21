#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"

#include <cstdarg>
#include <vector>

#include "../../include/NavKit/module/NavKitSettings.h"

Logger::Logger()
    : messageCount(0),
      textPoolSize(0),
      logQueue(std::make_unique<rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>>()) {
    memset(messages, 0, sizeof(char*) * MAX_MESSAGES);
    logFile.open("NavKit.log", std::ios::out | std::ios::trunc);
}

void Logger::doLog(const char* msg, const int len) {
    if (!len) {
        return;
    }

    std::lock_guard lock(logMutex);
    if (logBuffer.size() >= MAX_MESSAGES - 1) {
        logBuffer.pop_front();
    } else {
        messageCount++;
    }
    logBuffer.push_back(msg);
}

int Logger::getLogCount() const {
    return messageCount;
}

std::deque<std::string>& Logger::getLogBuffer() {
    return logBuffer;
}

[[noreturn]] void Logger::logRunner() {
    Logger& logger = getInstance();
    while (true) {
        if (std::optional<std::pair<LogCategory, std::string>> message = logger.logQueue->try_pop(); message.
            has_value()) {
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
