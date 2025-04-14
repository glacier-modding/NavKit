#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/RecastDemo/SampleInterfaces.h"

Logger::Logger() {
    logQueue = new rsj::ConcurrentQueue<std::pair<LogCategory, std::string> >();
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
            if (message.value().first == RC_LOG_ERROR) {
                msg = "[ERROR] ";
            }
            if (message.value().first == RC_LOG_WARNING) {
                msg = "[WARN] ";
            }
            msg += message.value().second;
            recastAdapter.log(message.value().first, msg);
        }
    }
}

void Logger::log(LogCategory category, const char *message, ...) {
    const std::pair<LogCategory, std::string> logMessage = {category, message};
    getInstance().logQueue->push(logMessage);
}
