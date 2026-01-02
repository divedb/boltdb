#include "mylib.hh"
#include <iostream>

namespace mylib {

MyClass::MyClass() : name_("Default") {}

MyClass::MyClass(const std::string& name) : name_(name) {}

MyClass::~MyClass() {
    std::cout << "MyClass destroyed: " << name_ << std::endl;
}

void MyClass::SetName(const std::string& name) {
    name_ = name;
}

std::string MyClass::GetName() const {
    return name_;
}

int MyClass::GetVersion() {
    return 1;
}

int add(int a, int b) {
    return a + b;
}

double multiply(double a, double b) {
    return a * b;
}

std::string greet(const std::string& name) {
    return "Hello, " + name + "!";
}

} // namespace mylib
