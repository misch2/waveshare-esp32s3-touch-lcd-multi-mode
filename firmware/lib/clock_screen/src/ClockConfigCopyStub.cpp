// The dashboard only needs the bounded string-copy helpers from ClockConfig.
// Keep the NVS persistence implementation out of the prototype host.
#include "../../../../waveshare-hodiny/WaveshareHodiny/ClockConfig.h"

#include <cstring>

void clockConfigCopy(char* destination, size_t destinationSize,
                     const char* value) {
  if (destination == nullptr || destinationSize == 0) return;
  const char* source = value != nullptr ? value : "";
  std::strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

void clockConfigCopy(char* destination, size_t destinationSize,
                     const String& value) {
  clockConfigCopy(destination, destinationSize, value.c_str());
}

