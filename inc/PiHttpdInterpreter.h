#pragma once

#include <string>
#include <vector>
#include <map>

class PiHttpdInterpreter {
public:
    enum MethodType {
        MT_UNKNOWN = 0,
        MT_GET,
        MT_POST,
        MT_HEAD
    };

    struct Strings {
        std::vector<std::string> values;

        inline std::vector<std::string>::const_iterator begin() const { return values.begin();  }
        inline std::vector<std::string>::const_iterator end() const { return values.end(); }
        inline void push_back(const std::string& s) { values.push_back(s); }
        inline size_t size() const { return values.size(); }
        inline const std::string& operator[] (int i) const { return values[i]; }
    };

    PiHttpdInterpreter();
    int init(const char* str, size_t len);

    inline MethodType method() const { return mMethod; }
    inline const std::string& doc() const { return mDocPath; }
    inline const std::string& version() const { return mVersion; }
    const std::string* param(const std::string& key) const;
    const Strings& header(const std::string& key) const;

private:
    void doInit(const char* str, size_t len);
    void parseRequestLine(const std::string& line);
    void parseHeaderLine(const std::string& line);
    void parseUrl(const std::string& url);
    void parsePathParameter(const std::string& path);

    static MethodType toMethodType(const std::string& line);
    static std::string::size_type find2(
            const std::string& d1, const std::string& d2,
            const std::string& s,
            std::string::size_type pos,
            std::string::size_type* dlen);
    static void split3(char d1, char d2, char d3, const std::string& s, std::vector<std::string>& elems);

    MethodType mMethod;
    std::string mDocPath;
    std::string mVersion;
    std::map<std::string, Strings> mHeader;
    std::map<std::string, std::string> mParam;

    const Strings EMPTY;
};
