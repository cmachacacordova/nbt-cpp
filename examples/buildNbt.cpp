#include <iostream>
#include <sstream>

#include "nbt/nbt.h"

int main() {
  nbt::Builder builder("root");
  builder.add(nbt::intTag("answer", 42));
  std::ostringstream output(std::ios::binary);
  nbt::serialize(output, builder.build());
  std::cout << output.str().size() << " bytes\n";
}
