#pragma once
// The caller's progress and diagnostics callback, which may be null.
#include <string>

#include "extract.h"

namespace rd::extract {

class Logger {
public:
    explicit Logger(LogFn fn) : fn_(fn) {}
    void operator()(const std::string& line) const {
        if (fn_) fn_(line);
    }

private:
    LogFn fn_;
};

}  // namespace rd::extract
