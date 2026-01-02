#pragma once

#ifdef _WIN32
  #ifdef BUILD_DLL
    #define MYLIB_API __declspec(dllexport)
  #else
    #define MYLIB_API __declspec(dllimport)
  #endif
#else
  #define MYLIB_API __attribute__((visibility("default")))
#endif

#include <string>
#include <vector>

namespace mylib {

class MYLIB_API MyClass {
public:
  MyClass();
  explicit MyClass(const std::string& name);
  ~MyClass();

  void SetName(const std::string& name);
  std::string GetName() const;
    
  MYLIB_API static int getVersion();

private:
  std::string name_;
};

MYLIB_API int add(int a, int b);
MYLIB_API double multiply(double a, double b);
MYLIB_API std::string greet(const std::string& name);

} // namespace mylib
