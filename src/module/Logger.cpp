#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"

#include <cstdarg>
#include <vector>

Logger::Logger()
    : logQueue(std::make_unique<rsj::ConcurrentQueue<std::pair<LogCategory, std::string> > >()),
      debugLogsEnabled(false) {
}

[[noreturn]] void Logger::logRunner() {
    if (const BuildContext *buildContext = RecastAdapter::getInstance().buildContext; buildContext == nullptr) {
        throw std::exception("Logger not initialized.");
    }
    const Logger &logger = getInstance();
    const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    while (true) {
        if (std::optional<std::pair<LogCategory, std::string> > message = logger.logQueue->try_pop(); message.
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
                    if (!logger.debugLogsEnabled) {
                        continue;
                    };
                    msg = "[DEBUG] ";
                    break;
                default:
                    break;
            }
            msg += message.value().second;
            recastAdapter.log(message.value().first, msg);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void Logger::log(LogCategory category, const char *format, ...) {
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
