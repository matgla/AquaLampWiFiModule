#pragma once

#include <string>

namespace logger
{

class ILoggerBase
{
public:
    ILoggerBase() = default;
    ~ILoggerBase() = default;
    ILoggerBase(const ILoggerBase&) = default;
    ILoggerBase(ILoggerBase&&) = default;
    ILoggerBase& operator=(const ILoggerBase&&) = delete;
    ILoggerBase& operator=(const ILoggerBase&) = delete;
    virtual ILoggerBase& debug(const std::string& componentName) = 0;
    virtual ILoggerBase& info(const std::string& componentName) = 0;
    virtual ILoggerBase& warn(const std::string& componentName) = 0;
    virtual ILoggerBase& error(const std::string& componentName) = 0;
};

} // namespace logger
