//   setprop <key> <value>
#include <cstdio>

#include "framework/system_properties.h"

using namespace mini;

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: setprop <key> <value>\n");
    return 2;
  }
  Status s = SystemProperties::set(argv[1], argv[2]);
  if (s != Status::Ok) {
    std::fprintf(stderr, "setprop: %s\n", toString(s));
    return 1;
  }
  return 0;
}
