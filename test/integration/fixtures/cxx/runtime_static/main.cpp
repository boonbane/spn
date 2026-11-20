#include <string>

int main() {
  std::string s = "runtime";
  s += " static";
  return s.size() == 14 ? 0 : 1;
}
