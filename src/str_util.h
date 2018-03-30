#ifndef _STR_UTIL_H_
#define _STR_UTIL_H_

#include <string>

template <typename T>
std::string to_string(T in)
{
  std::ostringstream ss;
  ss << in;
  return ss.str();
}

#endif
