#include "../include/NavKit/Logger.h"
#include "../include/RecastDemo/SampleInterfaces.h"

[[noreturn]] void Logger::logRunner() {
    if (getInstance().m_buildContext == nullptr) {
        throw std::exception("Logger not initialized.");
    }
    while (true) {
        if (std::optional<std::pair<rcLogCategory, std::string> > message = getInstance().logQueue.try_pop(); message.has_value()) {
            getInstance().m_buildContext->log(message.value().first, message.value().second.c_str());
        }
    }
}

void Logger::log(rcLogCategory category, const char *message, ...) {
    const std::pair<rcLogCategory, std::string> logMessage = {category, message};
    getInstance().logQueue.push(logMessage);
}
