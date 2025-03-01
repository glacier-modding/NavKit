#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/RecastDemo/SampleInterfaces.h"

Logger::Logger() {
    logQueue = new rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>();
}

[[noreturn]] void Logger::logRunner() {
    BuildContext *buildContext = RecastAdapter::getInstance().buildContext;
    if (buildContext == nullptr) {
        throw std::exception("Logger not initialized.");
    }
    Logger &logger = getInstance();
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    while (true) {
        if (std::optional<std::pair<LogCategory, std::string> > message = logger.logQueue->try_pop(); message.
            has_value()) {
            recastAdapter.log(static_cast<int>(message.value().first), message.value().second);
        }
    }
}

void Logger::log(LogCategory category, const char *message, ...) {
    const std::pair<LogCategory, std::string> logMessage = {category, message};
    getInstance().logQueue->push(logMessage);
}
