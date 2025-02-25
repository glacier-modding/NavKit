#pragma once
#include <string>
#include "../ConcurrentQueue/ConcurrentQueue.h"
#include <Recast.h>


class BuildContext;

class Logger {
    explicit Logger() {
        m_buildContext = nullptr;
        logQueue = rsj::ConcurrentQueue<std::pair<rcLogCategory, std::string> >();
    }

    BuildContext *m_buildContext;
    rsj::ConcurrentQueue<std::pair<rcLogCategory, std::string> > logQueue;

public:
    static Logger &getInstance() {
        static Logger instance;
        return instance;
    }

    void setBuildContext(BuildContext *buildContext) {
        m_buildContext = buildContext;
    }
    static void log(rcLogCategory category, const char *message, ...);

    static void logRunner();
};
