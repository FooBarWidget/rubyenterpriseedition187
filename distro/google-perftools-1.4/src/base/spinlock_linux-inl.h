/* Copyright (c) 2009, Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * This file is a Linux-specific part of spinlock.cc
 */

#include <sched.h>
#include <time.h>
#include "base/linux_syscall_support.h"

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static bool have_futex;

namespace {
static struct InitModule {
  InitModule() {
    int x = 0;
    // futexes are ints, so we can use them only when
    // that's the same size as the lockword_ in SpinLock.
    have_futex = (sizeof (Atomic32) == sizeof (int) && 
                  sys_futex(&x, FUTEX_WAKE, 1, 0) >= 0);
  }
} init_module;
}  // anonymous namespace

static void SpinLockWait(volatile Atomic32 *w) {
  int save_errno = errno;
  struct timespec tm;
  tm.tv_sec = 0;
  if (have_futex) {
    int value;
    tm.tv_nsec = 1000000;   // 1ms; really we're trying to sleep for one kernel
                            // clock tick
    while ((value = base::subtle::Acquire_CompareAndSwap(w, 0, 1)) != 0) {
      sys_futex(reinterpret_cast<int *>(const_cast<Atomic32 *>(w)),
          FUTEX_WAIT, value, reinterpret_cast<struct kernel_timespec *>(&tm));
    }
  } else {
    tm.tv_nsec = 2000001;       // above 2ms so linux 2.4 doesn't spin
    if (base::subtle::NoBarrier_Load(w) != 0) {
      sched_yield();
    }
    while (base::subtle::Acquire_CompareAndSwap(w, 0, 1) != 0) {
      nanosleep(&tm, NULL);
    }
  }
  errno = save_errno;
}

static void SpinLockWake(volatile Atomic32 *w) {
  if (have_futex) {
    sys_futex(reinterpret_cast<int *>(const_cast<Atomic32 *>(w)),
              FUTEX_WAKE, 1, 0);
  }
}
