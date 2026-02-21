#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"

#include <cstdarg>
#include <vector>

void Logger::doResetLog() {
    m_messageCount = 0;
    m_textPoolSize = 0;
    m_logBuffer.clear();
}

void Logger::doLog(const rcLogCategory category, const char* msg, const int len) {
    if (!len)
        return;

    std::lock_guard lock(m_log_mutex);
    if (m_logBuffer.size() >= MAX_MESSAGES - 1) {
        m_logBuffer.pop_front();
    } else {
        m_messageCount++;
    }
    m_logBuffer.push_back(msg);
    //if (m_messageCount >= MAX_MESSAGES) {
    //	//dumpLog("%s");
    //	resetLog();
    //	return;
    //}
    //char* dst = &m_textPool[m_textPoolSize];
    //int n = TEXT_POOL_SIZE - m_textPoolSize;
    //if (n < 2) {
    //	//dumpLog("%s");
    //	resetLog();
    //	dst = &m_textPool[m_textPoolSize];
    //	n = TEXT_POOL_SIZE - m_textPoolSize;
    //}

    //char* cat = dst;
    //char* text = dst+1;
    //const int maxtext = n-1;
    //// Store category
    //*cat = (char)category;
    //// Store message
    //const int count = rcMin(len+1, maxtext);
    //memcpy(text, msg, count);
    //text[count-1] = '\0';
    //m_textPoolSize += 1 + count;
    //m_messages[m_messageCount++] = dst;
}

void Logger::dumpLog(const char* format, ...) {
    // Print header.
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    printf("\n");

    // Print messages
    const int TAB_STOPS[4] = {28, 36, 44, 52};
    for (int i = 0; i < m_messageCount; ++i) {
        const char* msg = m_messages[i] + 1;
        int n = 0;
        while (*msg) {
            if (*msg == '\t') {
                int count = 1;
                for (int j = 0; j < 4; ++j) {
                    if (n < TAB_STOPS[j]) {
                        count = TAB_STOPS[j] - n;
                        break;
                    }
                }
                while (--count) {
                    putchar(' ');
                    n++;
                }
            } else {
                putchar(*msg);
                n++;
            }
            msg++;
        }
        putchar('\n');
    }
}

int Logger::getLogCount() const {
    return m_messageCount;
}

std::deque<std::string>& Logger::getLogBuffer() {
    return m_logBuffer;
}

const char* Logger::getLogText(const int i) const {
    if (i < m_logBuffer.size()) {
        return m_logBuffer[i].c_str();
    } else {
        return "";
    }
}

Logger::Logger()
    : m_messageCount(0),
      m_textPoolSize(0),
      logQueue(std::make_unique<rsj::ConcurrentQueue<std::pair<LogCategory, std::string>>>()),
      debugLogsEnabled(false) {
    memset(m_messages, 0, sizeof(char*) * MAX_MESSAGES);
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
                if (!logger.debugLogsEnabled) {
                    continue;
                };
                msg = "[DEBUG] ";
                break;
            default:
                break;
            }
            msg += message.value().second;
            logger.doLog(static_cast<rcLogCategory>(message.value().first), msg.c_str(), msg.length());
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
    return m_log_mutex;
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
