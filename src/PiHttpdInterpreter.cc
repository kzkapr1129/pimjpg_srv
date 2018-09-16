#include "PiHttpdInterpreter.h"
#include "PiException.h"
#include <stdio.h>
#include <sstream>
#include <vector>
#include <map>

PiHttpdInterpreter::PiHttpdInterpreter() : mMethod(MT_UNKNOWN) {
}

int PiHttpdInterpreter::init(const char* str, size_t len) {
    TRAP1(exception, msg, doInit(str, len););
    if (exception) {
        fprintf(stderr, "PiHttpdInterpreter::init err=%s\n", msg.c_str());
        return -1;
    }
    return 0;
}

const std::string* PiHttpdInterpreter::param(const std::string& key) const {
    if (mParam.find(key) == mParam.end()) {
        return NULL;
    }
    return &mParam.at(key);
}

const PiHttpdInterpreter::Strings& PiHttpdInterpreter::header(const std::string& key) const {
    std::map<std::string, Strings>::const_iterator val = mHeader.find(key);
    if (val == mHeader.end()) {
        return EMPTY;
    }
    return val->second;
}

void PiHttpdInterpreter::doInit(const char* str, size_t len) {
    std::vector<std::string> lines;
    std::string s(str, len);
    std::string::size_type dlen = 0;
    std::string::size_type pos = 0;

    while (pos != std::string::npos) {
        std::string::size_type pos_end = find2("\r\n", "\n", s, pos, &dlen);
        if (pos_end != std::string::npos) {
            size_t len = pos_end - pos;
            std::string line = s.substr(pos, len);
            if (line.length() > 2) {
                lines.push_back(line);
            }

            pos_end += dlen;
        }
        pos = pos_end;
    }

    std::vector<std::string>::iterator request_line = lines.begin();
    if (request_line != lines.end()) {
        parseRequestLine(*request_line);

        std::vector<std::string>::iterator header_lines = ++request_line;
        if (header_lines != lines.end()) {
            for (; header_lines != lines.end(); header_lines++) {
                parseHeaderLine(*header_lines);
            }
        }
    }
}

void PiHttpdInterpreter::parseRequestLine(const std::string& line) {
    std::vector<std::string> elems;
    std::string item;
    std::stringstream ss(line);

    while (std::getline(ss, item, ' ')) {
        if (!item.empty()) {
            elems.push_back(item);
        }
    }

    size_t num_elems = elems.size();
    if (num_elems >= 1) mMethod = toMethodType(elems[0]);
    if (num_elems >= 2) parseUrl(elems[1]);
    if (num_elems >= 3) mVersion = elems[2];
}

void PiHttpdInterpreter::parseHeaderLine(const std::string& line) {
    std::vector<std::string> elems;
    split3(':', ' ', ',', line, elems);

    if (elems.size() > 0) {
        std::vector<std::string>::iterator header_name = elems.begin();
        const std::string& key = *header_name;

        std::vector<std::string>::iterator val = ++header_name;
        if (header_name != elems.end() && val != elems.end()) {
            Strings vals;
            for (; val != elems.end(); val++) {
                vals.push_back(*val);
            }
            mHeader[key] = vals;
        }
    }
}

PiHttpdInterpreter::MethodType PiHttpdInterpreter::toMethodType(const std::string& line) {
    if (!line.compare("GET")) {
        return MT_GET;
    } else if (!line.compare("POST")) {
        return MT_POST;
    } else if (!line.compare("HEAD")) {
        return MT_HEAD;
    }

    return MT_UNKNOWN;
}

void PiHttpdInterpreter::parseUrl(const std::string& url) {
    std::string::size_type dlen;
    std::string::size_type protocol_pos = find2("http://", "https://", url, 0, &dlen);
   if (protocol_pos == std::string::npos) {
        mDocPath = url;
        parsePathParameter(url);
   } else {
        std::string::size_type spos = protocol_pos + dlen;
        std::string::size_type path_start = url.find("/", spos);
        if (path_start == std::string::npos) {
            mDocPath = "/";
        } else {
            std::string::size_type path_len = url.length() - path_start;
            std::string path = url.substr(path_start, path_len);
            parsePathParameter(path);
        }
    }
}

void PiHttpdInterpreter::parsePathParameter(const std::string& path) {
    std::string::size_type param_start = path.find("?", 0);
    if (param_start == std::string::npos) {
        mDocPath = path;
    } else {
        // TODO: Must remove the following line:
        mDocPath = path.substr(0, param_start);

        std::string::size_type delimitor_len = 1; // length of '?'
        param_start += delimitor_len;
        std::string::size_type plen = path.length() - param_start;
        std::string params = path.substr(param_start, plen);

        std::string param;
        std::stringstream ss(params);
        while (std::getline(ss, param, '&')) {
            if (!param.empty()) {
                std::string::size_type equal_pos = param.find("=");
                if (equal_pos == std::string::npos) {
                    mParam[param] = "";
                } else {
                    std::string key = param.substr(0, equal_pos);
                    equal_pos += delimitor_len;
                    std::string::size_type value_len = param.length() - equal_pos;
                    std::string value = param.substr(equal_pos, value_len);
                    mParam[key] = value;
                }
            }
        }
    }
}

std::string::size_type PiHttpdInterpreter::find2(
            const std::string& d1, const std::string& d2,
            const std::string& s,
            std::string::size_type pos,
            std::string::size_type* dlen) {

    if (dlen == NULL)
        return std::string::npos;

    *dlen = 0;

    std::string::size_type p = std::string::npos;
    if ((p = s.find(d1, pos)) != std::string::npos) {
        *dlen = d1.length();
    } else if ((p = s.find(d2, pos)) != std::string::npos) {
        *dlen = d2.length();
    }

    return p;
}

void PiHttpdInterpreter::split3(char d1, char d2, char d3, const std::string& s, std::vector<std::string>& elems) {
    elems.clear();

    std::string item;
    for (int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c != d1 && c != d2 && c != d3) {
            item += c;
        } else if (item.length() > 0) {
            elems.push_back(item);
            item = "";
        }
    }

    if (item.length() > 0) {
        elems.push_back(item);
    }
}

