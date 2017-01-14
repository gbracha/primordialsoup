// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/os_thread.h"

#include "vm/lockers.h"
#include "vm/thread.h"

namespace psoup {

// The single thread local key which stores all the thread local data
// for a thread.
ThreadLocalKey OSThread::thread_key_ = kUnsetThreadLocalKey;
OSThread* OSThread::thread_list_head_ = NULL;
Mutex* OSThread::thread_list_lock_ = NULL;
bool OSThread::creation_enabled_ = false;


OSThread::OSThread() :
    BaseThread(true),
    id_(OSThread::GetCurrentThreadId()),
    join_id_(OSThread::GetCurrentThreadJoinId()),
    trace_id_(OSThread::GetCurrentThreadTraceId()),
    name_(NULL),
    thread_list_next_(NULL),
    thread_(NULL) {
}


OSThread* OSThread::CreateOSThread() {
  ASSERT(thread_list_lock_ != NULL);
  MutexLocker ml(thread_list_lock_);
  if (!creation_enabled_) {
    return NULL;
  }
  OSThread* os_thread = new OSThread();
  AddThreadToListLocked(os_thread);
  return os_thread;
}


OSThread::~OSThread() {
  RemoveThreadFromList(this);
  free(name_);
}


static void DeleteThread(void* thread) {
  delete reinterpret_cast<OSThread*>(thread);
}


void OSThread::Startup() {
  // Allocate the global OSThread lock.
  ASSERT(thread_list_lock_ == NULL);
  thread_list_lock_ = new Mutex();
  ASSERT(thread_list_lock_ != NULL);

  // Create the thread local key.
  ASSERT(thread_key_ == kUnsetThreadLocalKey);
  thread_key_ = CreateThreadLocal(DeleteThread);
  ASSERT(thread_key_ != kUnsetThreadLocalKey);

  // Enable creation of OSThread structures in the VM.
  EnableOSThreadCreation();

  // Create a new OSThread strcture and set it as the TLS.
  OSThread* os_thread = CreateOSThread();
  ASSERT(os_thread != NULL);
  OSThread::SetCurrent(os_thread);
  os_thread->set_name("Dart_Initialize");
}


void OSThread::Shutdown() {
  // We cannot delete the thread local key and thread list lock,  yet.
  // See the note on thread_list_lock_ in os_thread.h.
#if 0
  if (thread_list_lock_ != NULL) {
    // Delete the thread local key.
    ASSERT(thread_key_ != kUnsetThreadLocalKey);
    DeleteThreadLocal(thread_key_);
    thread_key_ = kUnsetThreadLocalKey;

    // Delete the global OSThread lock.
    ASSERT(thread_list_lock_ != NULL);
    delete thread_list_lock_;
    thread_list_lock_ = NULL;
  }
#endif
}


OSThread* OSThread::CreateAndSetUnknownThread() {
  ASSERT(OSThread::GetCurrentTLS() == NULL);
  OSThread* os_thread = CreateOSThread();
  if (os_thread != NULL) {
    OSThread::SetCurrent(os_thread);
    os_thread->set_name("Unknown");
  }
  return os_thread;
}


bool OSThread::IsThreadInList(ThreadJoinId join_id) {
  if (join_id == OSThread::kInvalidThreadJoinId) {
    return false;
  }
  OSThreadIterator it;
  while (it.HasNext()) {
    ASSERT(OSThread::thread_list_lock_->IsOwnedByCurrentThread());
    OSThread* t = it.Next();
    // An address test is not sufficient because the allocator may recycle
    // the address for another Thread. Test against the thread's join id.
    if (t->join_id() == join_id) {
      return true;
    }
  }
  return false;
}


void OSThread::DisableOSThreadCreation() {
  MutexLocker ml(thread_list_lock_);
  creation_enabled_ = false;
}


void OSThread::EnableOSThreadCreation() {
  MutexLocker ml(thread_list_lock_);
  creation_enabled_ = true;
}


OSThread* OSThread::GetOSThreadFromThread(Thread* thread) {
  ASSERT(thread->os_thread() != NULL);
  return thread->os_thread();
}


void OSThread::AddThreadToListLocked(OSThread* thread) {
  ASSERT(thread != NULL);
  ASSERT(thread_list_lock_ != NULL);
  ASSERT(OSThread::thread_list_lock_->IsOwnedByCurrentThread());
  ASSERT(creation_enabled_);
  ASSERT(thread->thread_list_next_ == NULL);

#if defined(DEBUG)
  {
    // Ensure that we aren't already in the list.
    OSThread* current = thread_list_head_;
    while (current != NULL) {
      ASSERT(current != thread);
      current = current->thread_list_next_;
    }
  }
#endif

  // Insert at head of list.
  thread->thread_list_next_ = thread_list_head_;
  thread_list_head_ = thread;
}


void OSThread::RemoveThreadFromList(OSThread* thread) {
  bool final_thread = false;
  {
    ASSERT(thread != NULL);
    ASSERT(thread_list_lock_ != NULL);
    MutexLocker ml(thread_list_lock_);
    OSThread* current = thread_list_head_;
    OSThread* previous = NULL;

    // Scan across list and remove |thread|.
    while (current != NULL) {
      if (current == thread) {
        // We found |thread|, remove from list.
        if (previous == NULL) {
          thread_list_head_ = thread->thread_list_next_;
        } else {
          previous->thread_list_next_ = current->thread_list_next_;
        }
        thread->thread_list_next_ = NULL;
        final_thread = !creation_enabled_  && (thread_list_head_ == NULL);
        break;
      }
      previous = current;
      current = current->thread_list_next_;
    }
  }
  // Check if this is the last thread. The last thread does a cleanup
  // which removes the thread local key and the associated mutex.
  if (final_thread) {
    Shutdown();
  }
}


void OSThread::SetCurrent(OSThread* current) {
  OSThread::SetThreadLocal(thread_key_, reinterpret_cast<uword>(current));
}


OSThreadIterator::OSThreadIterator() {
  ASSERT(OSThread::thread_list_lock_ != NULL);
  // Lock the thread list while iterating.
  OSThread::thread_list_lock_->Lock();
  next_ = OSThread::thread_list_head_;
}


OSThreadIterator::~OSThreadIterator() {
  ASSERT(OSThread::thread_list_lock_ != NULL);
  // Unlock the thread list when done.
  OSThread::thread_list_lock_->Unlock();
}


bool OSThreadIterator::HasNext() const {
  ASSERT(OSThread::thread_list_lock_ != NULL);
  ASSERT(OSThread::thread_list_lock_->IsOwnedByCurrentThread());
  return next_ != NULL;
}


OSThread* OSThreadIterator::Next() {
  ASSERT(OSThread::thread_list_lock_ != NULL);
  ASSERT(OSThread::thread_list_lock_->IsOwnedByCurrentThread());
  OSThread* current = next_;
  next_ = next_->thread_list_next_;
  return current;
}

}  // namespace psoup
