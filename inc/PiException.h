#pragma once

#include <exception>
#include <string>

#define TRAP1(catched, msg, functions) \
        bool catched = false; \
        std::string msg; \
        try { \
            functions; \
        } catch(std::exception e) { \
            msg = e.what(); \
            catched = true; \
        } catch(...) { \
            catched = true; \
        } \

#define TRAP2(catched, msg, functions) \
        catched = false; \
        msg = ""; \
        try { \
            functions; \
        } catch(std::exception e) { \
            msg = e.what(); \
            catched = true; \
        } catch(...) { \
            catched = true; \
        } \

#define TRAP_LOG(functions) \
        try { \
            functions; \
        } catch(std::exception e) { \
            fprintf(stderr, "Error %s:%d msg=%s\n", __FILE__, __LINE__, e.what()); \
        } catch(...) { \
            fprintf(stderr, "Unknown Error %s:%d\n", __FILE__, __LINE__); \
        } \

#define TRAP_IGN(functions) \
        try { \
            functions; \
        } catch(...) { \
        } \

