#pragma once

#include <exception>
#include <string>

class CIVCException : public std::exception
{
public:
    CIVCException(const std::string& type, const std::string& message, const std::string& function)
        : m_type(type), m_message(message), m_function(function)
    {
        m_what = m_type + ": " + m_message + " (" + m_function + ")";
    }

    virtual ~CIVCException() throw() {}

    const char* what() const throw() override
    {
        return m_what.c_str();
    }

    const std::string& GetType() const { return m_type; }
    const std::string& GetMessage() const { return m_message; }
    const std::string& GetFunction() const { return m_function; }

private:
    std::string m_type;
    std::string m_message;
    std::string m_function;
    std::string m_what;
};
