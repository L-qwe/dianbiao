
#pragma once
struct SilentLog{template<class...T>void printf(T...){ }template<class T>void println(const T&){ }template<class T>void print(const T&){ }};
inline SilentLog AppLog;
