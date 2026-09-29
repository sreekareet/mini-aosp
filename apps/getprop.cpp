//   getprop [key]      print one value, or every property as "[key]: [value]"
#include <cstdio>

#include "framework/system_properties.h"

using namespace mini;

int main(int argc, char** argv) {
  if (argc == 2) {
    auto v = SystemProperties::get(argv[1]);
    if (!v) {
      if (v.status() != Status::NotFound) std::fprintf(stderr, "getprop: %s\n", toString(v.status()));
      return 1;
    }
    std::printf("%s\n", v.value().c_str());
    return 0;
  }
  if (argc != 1) {
    std::fprintf(stderr, "usage: getprop [key]\n");
    return 2;
  }
  auto all = SystemProperties::list();
  if (!all) {
    std::fprintf(stderr, "getprop: %s\n", toString(all.status()));
    return 1;
  }
  for (const auto& [k, v] : all.value()) std::printf("[%s]: [%s]\n", k.c_str(), v.c_str());
  return 0;
}
