// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_OS_FUCHSIA)

#include "vm/os.h"

#include <errno.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdarg.h>
#include <unistd.h>

#include "vm/assert.h"

namespace psoup {

void OS::Startup() {}
void OS::Shutdown() {}


int64_t OS::CurrentMonotonicMicros() {
  return mx_time_get(MX_CLOCK_MONOTONIC) / kNanosecondsPerMicrosecond;
}


int OS::NumberOfAvailableProcessors() {
  return sysconf(_SC_NPROCESSORS_CONF);
}


void OS::DebugBreak() {
  UNIMPLEMENTED();
}


static void VFPrint(FILE* stream, const char* format, va_list args) {
  vfprintf(stream, format, args);
  fflush(stream);
}


void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(stdout, format, args);
  va_end(args);
}


void OS::PrintErr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(stderr, format, args);
  va_end(args);
}


char* OS::PrintStr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list measure_args;
  va_copy(measure_args, args);
  intptr_t len = vsnprintf(NULL, 0, format, measure_args);
  va_end(measure_args);

  char* buffer = reinterpret_cast<char*>(malloc(len + 1));

  va_list print_args;
  va_copy(print_args, args);
  int r = vsnprintf(buffer, len + 1, format, print_args);
  ASSERT(r >= 0);
  va_end(print_args);
  va_end(args);
  return buffer;
}


void OS::Abort() {
  abort();
}


void OS::Exit(int code) {
  UNIMPLEMENTED();
}

}  // namespace psoup

#endif  // defined(TARGET_OS_FUCHSIA)
