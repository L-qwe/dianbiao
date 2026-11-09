
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <type_traits>
#include <sstream>
#include <iomanip>
class String:public std::string {
public:
 using std::string::string;using std::string::operator=;
 String()=default;String(const std::string& s):std::string(s){}
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0>String(T n):std::string(std::to_string(n)){}
 String(double n,int digits){std::ostringstream o;o<<std::fixed<<std::setprecision(digits)<<n;assign(o.str());}
 void replace(const char* from,const String& to){size_t at=0;while((at=find(from,at))!=npos){std::string::replace(at,strlen(from),to);at+=to.size();}}
 String substring(unsigned begin,unsigned end)const{return substr(begin,end-begin);}
 String substring(unsigned begin)const{return substr(begin);}
 int indexOf(char ch,unsigned start=0)const{auto at=find(ch,start);return at==npos?-1:int(at);}
 int indexOf(const char* s,unsigned start=0)const{auto at=find(s,start);return at==npos?-1:int(at);}
 long toInt()const{return strtol(c_str(),nullptr,10);}
 bool reserve(size_t n){std::string::reserve(n);return true;}
 bool concat(const char* s){append(s);return true;}
 bool isEmpty()const{return empty();}
 bool startsWith(const char* s)const{return rfind(s,0)==0;}
 bool endsWith(const char* s)const{size_t n=strlen(s);return size()>=n && compare(size()-n,n,s)==0;}
 void remove(unsigned at){erase(at);}
 void remove(unsigned at,unsigned count){erase(at,count);}
 friend String operator+(const String&a,const String&b){return std::string(a)+std::string(b);}
};
inline uint32_t fakeMillis=0;
inline uint32_t millis(){return fakeMillis;}
inline void delay(unsigned ms){fakeMillis+=ms;}
#define F(x) x
