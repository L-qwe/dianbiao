#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
struct RecordingLog {
    std::vector<std::string> lines;
    void printf(const char* format, ...) {
        char line[512];
        va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
        lines.emplace_back(line);
    }
    template<class T> void println(const T& line){lines.emplace_back(line);}
};
inline RecordingLog AppLog;
