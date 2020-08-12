// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/heap.h"

#include "vm/interpreter.h"
#include "vm/isolate.h"
#include "vm/os.h"
#include "vm/random.h"

namespace psoup {

Heap::Heap() :
    heap_size_(0),
    class_table_(nullptr),
    class_table_size_(0),
    class_table_capacity_(0),
    class_table_free_(0),
    worklist_(nullptr),
    worklist_size_(0),
    worklist_capacity_(0),
    table_(nullptr),
    table_size_(0),
    table_capacity_(0),
    interpreter_(nullptr),
    handles_(),
    handles_size_(0),
    max_gc_time_(0),
    total_gc_time_(0),
    gc_count_(0) {
  // Class table.
  class_table_capacity_ = 1024;
  class_table_ = new Object[class_table_capacity_];
#if defined(DEBUG)
  for (intptr_t i = 0; i < kFirstRegularObjectCid; i++) {
    class_table_[i] = static_cast<Object>(kUninitializedWord);
  }
  for (intptr_t i = kFirstRegularObjectCid; i < class_table_capacity_; i++) {
    class_table_[i] = static_cast<Object>(kUnallocatedWord);
  }
#endif
  class_table_size_ = kFirstRegularObjectCid;

  worklist_capacity_ = KB;
  worklist_ = reinterpret_cast<HeapObject*>(
      malloc(sizeof(HeapObject) * worklist_capacity_));

  table_capacity_ = 8 * KB;
  table_ = reinterpret_cast<HeapObject*>(
      malloc(sizeof(HeapObject) * table_capacity_));
  table_size_ = 1;
}

Heap::~Heap() {
  delete[] class_table_;
  free(worklist_);
  free(table_);

  OS::PrintErr("max-gc: %" Pd64 " ns, "
               "total-gc: %" Pd64 " ns, "
               "gc-count: %" Pd "\n",
               max_gc_time_, total_gc_time_, gc_count_);
}

Message Heap::AllocateMessage() {
  Behavior behavior = interpreter_->object_store()->Message();
  ASSERT(behavior->IsRegularObject());
  behavior->AssertCouldBeBehavior();
  SmallInteger id = behavior->id();
  if (id == interpreter_->nil_obj()) {
    id = SmallInteger::New(AllocateClassId());  // SAFEPOINT
    behavior = interpreter_->object_store()->Message();
    RegisterClass(id->value(), behavior);
  }
  ASSERT(id->IsSmallInteger());
  SmallInteger format = behavior->format();
  ASSERT(format->IsSmallInteger());
  intptr_t num_slots = format->value();
  ASSERT(num_slots == 2);
  Object new_instance = AllocateRegularObject(id->value(),
                                              num_slots);
  return static_cast<Message>(new_instance);
}

uword Heap::Allocate(intptr_t size, Allocator allocator) {
  if (allocator != kSnapshot) {
    GCStep();
  }

  uword addr = reinterpret_cast<uword>(malloc(size));
  if (addr == 0) {
    FATAL1("Failed to allocate %" Pd " bytes\n", size);
  }
  heap_size_ += size;
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUninitializedByte, size);
#endif
  return addr;
}

static void ForwardClass(Heap* heap, HeapObject object) {
  ASSERT(object->IsHeapObject());
  Behavior old_class = heap->ClassAt(object->cid());
  if (old_class->IsForwardingCorpse()) {
    Behavior new_class = static_cast<Behavior>(
        static_cast<ForwardingCorpse>(old_class)->target());
    ASSERT(!new_class->IsForwardingCorpse());
    new_class->AssertCouldBeBehavior();
    ASSERT(new_class->id()->IsSmallInteger());
    object->set_cid(new_class->id()->value());
  }
}

static void ForwardPointer(Object* ptr) {
  Object old_target = *ptr;
  if (old_target->IsForwardingCorpse()) {
    Object new_target = static_cast<ForwardingCorpse>(old_target)->target();
    ASSERT(!new_target->IsForwardingCorpse());
    *ptr = new_target;
  }
}

static void ForwardPointer(Ref* ptr) {
  Object old_target = ptr->to;
  if (old_target->IsForwardingCorpse()) {
    Object new_target = static_cast<ForwardingCorpse>(old_target)->target();
    ASSERT(!new_target->IsForwardingCorpse());
    ptr->UpdateNoCheck(new_target);
  }
}

void Heap::GCStep() {
  int64_t start = OS::CurrentMonotonicNanos();

  for (intptr_t round = 0; round < 3; round++) {
    intptr_t index = interpreter_->isolate()->random().NextUInt64() %
        table_size_;
    if (index == 0) continue;  // Unused entry.

    HeapObject candidate = table_[index];
    ASSERT(candidate->table_index() == index);
    ASSERT(candidate->cid() >= kFirstLegalCid);

    if (CheckReachable(candidate)) {
      for (intptr_t i = 0; i < worklist_size_; i++) {
        worklist_[i]->set_is_marked(false);
        worklist_[i]->set_is_weak_referent(false);
      }
    } else {
      bool includes_klass = false;
      for (intptr_t i = 0; i < worklist_size_; i++) {
        if (worklist_[i]->in_class_table()) {
          includes_klass = true;
        }
        Unlink(worklist_[i]);
      }
      for (intptr_t i = 0; i < worklist_size_; i++) {
        Free(worklist_[i]);
      }
      if (includes_klass) {
        interpreter_->ClearCache();
      }
    }
    worklist_size_ = 0;
  }

  int64_t stop = OS::CurrentMonotonicNanos();
  int64_t time = stop - start;
  if (time > max_gc_time_) {
    max_gc_time_ = time;
  }
  total_gc_time_ += time;
  gc_count_++;
}

bool Heap::CheckReachable(HeapObject obj) {
  if ((obj == interpreter_->nil_obj()) ||
      (obj == interpreter_->true_obj()) ||
      (obj == interpreter_->false_obj())) {
    // The incoming lists of these objects are very large. Note these are
    // reachable immediately without pushing to the worklist.
    return true;
  }

  ASSERT(!obj->is_marked());
  obj->set_is_marked(true);
  ASSERT(worklist_size_ == 0);
  WorklistPush(obj);

  for (intptr_t cursor = 0; cursor < worklist_size_; cursor++) {
    HeapObject obj = worklist_[cursor];

    for (intptr_t i = 0; i < handles_size_; i++) {
      if (*handles_[i] == obj) {
        return true;
      }
    }

    bool is_weak_referent = false;
    Link* incoming_head = obj->incoming();
    for (Link* incoming = incoming_head->next;
         incoming != incoming_head;
         incoming = incoming->next) {
      Ref* ref = static_cast<Ref*>(incoming);
      ASSERT(ref->to == obj);
      HeapObject source = static_cast<HeapObject>(ref->from);
      if (source == nullptr) {
        // This is a root.
        return true;
      }
      ASSERT(source.IsHeapObject());
      ASSERT(source->cid() != kFreeListElementCid);
      ASSERT(source->cid() != kForwardingCorpseCid);
      if (source->cid() == kWeakArrayCid) {
        is_weak_referent = true;
      } else if (!source->is_marked()) {
        source->set_is_marked(true);
        WorklistPush(source);
      }
    }
    if (is_weak_referent) {
      obj.set_is_weak_referent(true);
    }
  }

  return false;
}

void Heap::WorklistPush(HeapObject obj) {
  if (worklist_size_ == worklist_capacity_) {
    worklist_capacity_ = worklist_capacity_ + (worklist_capacity_ >> 1);
    worklist_ = reinterpret_cast<HeapObject*>(
        realloc(worklist_, sizeof(HeapObject)*worklist_capacity_));
    if (TRACE_GROWTH) {
      OS::PrintErr("Growing worklist to capacity %" Pd "\n",
                   worklist_capacity_);
    }
  }
  worklist_[worklist_size_++] = obj;
}

void Heap::TableGrow() {
  table_capacity_ = table_capacity_ + (table_capacity_ >> 1);
  table_ = reinterpret_cast<HeapObject*>(
      realloc(table_, sizeof(HeapObject)*table_capacity_));
  if (TRACE_GROWTH) {
    OS::PrintErr("Growing object table to  %" Pd "\n", table_capacity_);
  }
}

void Heap::CollectAll(Reason reason) {
  OS::Print("Ignoring explicit GC\n");
}

void Heap::Unlink(HeapObject obj) {
  if (obj.is_weak_referent()) {
    Link* incoming_head = obj->incoming();
    Link* next;
    for (Link* incoming = incoming_head->next;
         incoming != incoming_head;
         incoming = next) {
      next = incoming->next;
      Ref* ref = static_cast<Ref*>(incoming);
      ref->UpdateNoCheck(interpreter_->nil_obj());
    }
  }

  // Unlinking outgoing pointers, as some may point to objects not
  // reclaimed in this current GC cycle.
  Ref* from;
  Ref* to;
  obj->Pointers(&from, &to);
  for (Ref* ptr = from; ptr <= to; ptr++) {
    // Don't update any slots containing Smi. Some are length fields still
    // needed for obj->HeapSize, and the class id will be needed in Free.
    if (!ptr->to->IsSmallInteger()) {
      ptr->Update(obj, static_cast<HeapObject>(nullptr));
    }
  }

  intptr_t index = obj->table_index();
  ASSERT(index > 0 && index < table_size_);
  intptr_t last_index = table_size_ - 1;
  HeapObject last = table_[last_index];
  ASSERT(last->cid() >= kFirstLegalCid);
  ASSERT(last->table_index() == last_index);
  obj->set_table_index(0);
  table_[last_index] = obj;
  last->set_table_index(index);
  table_[index] = last;
  table_size_--;
}

void Heap::Free(HeapObject obj) {
  ASSERT(obj->incoming()->IsEmpty());

  if (obj->in_class_table()) {
    intptr_t cid = static_cast<Behavior>(obj)->id()->value();
    ASSERT(class_table_[cid] == obj);
    class_table_[cid] = SmallInteger::New(class_table_free_);
    class_table_free_ = cid;
  }

  heap_size_ -= obj->HeapSize();
  free(reinterpret_cast<void*>(obj.Addr()));
}

void Heap::MournClassTableForwarded() {
  for (intptr_t i = kFirstLegalCid; i < class_table_size_; i++) {
    Behavior old_class = static_cast<Behavior>(class_table_[i]);
    if (!old_class->IsForwardingCorpse()) {
      continue;
    }

    class_table_[i] = SmallInteger::New(class_table_free_);
    class_table_free_ = i;
  }
}

bool Heap::BecomeForward(Array old, Array neu) {
  if (old->Size() != neu->Size()) {
    return false;
  }

  intptr_t length = old->Size();
  if (TRACE_BECOME) {
    OS::PrintErr("become(%" Pd ")\n", length);
  }

  for (intptr_t i = 0; i < length; i++) {
    Object forwarder = old->element(i);
    Object forwardee = neu->element(i);
    if (forwarder->IsImmediateObject() ||
        forwardee->IsImmediateObject()) {
      return false;
    }
  }

  for (intptr_t i = 0; i < length; i++) {
    HeapObject forwarder = static_cast<HeapObject>(old->element(i));
    HeapObject forwardee = static_cast<HeapObject>(neu->element(i));

    ASSERT(!forwarder->IsForwardingCorpse());
    ASSERT(!forwardee->IsForwardingCorpse());

    forwardee->set_header_hash(forwarder->header_hash());
    forwardee->set_in_class_table(forwardee->in_class_table() ||
                                  forwarder->in_class_table());

    intptr_t heap_size = forwarder->HeapSize();

    Unlink(forwarder);

    HeapObject::Initialize(forwarder->Addr(), kForwardingCorpseCid, heap_size);
    ASSERT(forwarder->IsForwardingCorpse());
    ForwardingCorpse corpse = static_cast<ForwardingCorpse>(forwarder);
    if (forwarder->heap_size() == 0) {
      corpse->set_overflow_size(heap_size);
    }
    ASSERT(forwarder->HeapSize() == heap_size);

    corpse->set_target(forwardee);
  }

  ForwardClassIds();
  ForwardRoots();
  ForwardHeap();  // With forwarded class ids.
  MournClassTableForwarded();

  interpreter_->ClearCache();

  return true;
}

void Heap::ForwardRoots() {
  for (intptr_t i = 0; i < handles_size_; i++) {
    ForwardPointer(handles_[i]);
  }

  Ref* from;
  Ref* to;
  interpreter_->RootPointers(&from, &to);
  for (Ref* ptr = from; ptr <= to; ptr++) {
    ForwardPointer(ptr);
  }
  interpreter_->StackPointers(&from, &to);
  for (Ref* ptr = from; ptr <= to; ptr++) {
    ForwardPointer(ptr);
  }
}

void Heap::ForwardHeap() {
  for (intptr_t i = 1; i < table_size_; i++) {
    HeapObject obj = table_[i];
    ASSERT(obj->cid() >= kFirstLegalCid);
    ForwardClass(this, obj);
    Ref* from;
    Ref* to;
    obj->Pointers(&from, &to);
    for (Ref* ptr = from; ptr <= to; ptr++) {
      ForwardPointer(ptr);
    }
  }
}

void Heap::ForwardClassIds() {
  // For forwarded classes, use the cid of the old class. For most classes, we
  // could use the cid of the new class or a newlly allocated cid (provided all
  // the instances are updated). But for the classes whose representation is
  // defined by the VM we need to keep the fixed cids (e.g., kSmiCid), so we may
  // as well treat them all the same way.
  Object nil = interpreter_->nil_obj();
  for (intptr_t old_cid = kFirstLegalCid;
       old_cid < class_table_size_;
       old_cid++) {
    Behavior old_class = static_cast<Behavior>(class_table_[old_cid]);
    if (!old_class->IsForwardingCorpse()) {
      continue;
    }

    Behavior new_class = static_cast<Behavior>(
        static_cast<ForwardingCorpse>(old_class)->target());
    ASSERT(!new_class->IsForwardingCorpse());

    if (new_class->id() != nil) {
      ASSERT(new_class->id()->IsSmallInteger());
      // Arraynge for instances with new_cid to be migrated to i.
      intptr_t new_cid = new_class->id()->value();
      class_table_[new_cid] = old_class;
    }

    new_class->set_id(SmallInteger::New(old_cid));
    class_table_[old_cid] = new_class;
  }
}

intptr_t Heap::AllocateClassId() {
  intptr_t cid;
  if (class_table_free_ != 0) {
    cid = class_table_free_;
    class_table_free_ =
        static_cast<SmallInteger>(class_table_[cid])->value();
  } else if (class_table_size_ == class_table_capacity_) {
    if (TRACE_GROWTH) {
      OS::PrintErr("Scavenging to free class table entries\n");
    }
    CollectAll(kClassTable);
    if (class_table_free_ != 0) {
      cid = class_table_free_;
      class_table_free_ =
          static_cast<SmallInteger>(class_table_[cid])->value();
    } else {
      class_table_capacity_ += (class_table_capacity_ >> 1);
      if (TRACE_GROWTH) {
        OS::PrintErr("Growing class table to %" Pd "\n",
                     class_table_capacity_);
      }
      Object* old_class_table = class_table_;
      class_table_ = new Object[class_table_capacity_];
      for (intptr_t i = 0; i < class_table_size_; i++) {
        class_table_[i] = old_class_table[i];
      }
#if defined(DEBUG)
      for (intptr_t i = class_table_size_; i < class_table_capacity_; i++) {
        class_table_[i] = static_cast<Object>(kUnallocatedWord);
      }
#endif
      delete[] old_class_table;
      cid = class_table_size_;
      class_table_size_++;
    }
  } else {
    cid = class_table_size_;
    class_table_size_++;
  }
#if defined(DEBUG)
  class_table_[cid] = static_cast<Object>(kUninitializedWord);
#endif
  return cid;
}

void Heap::InitializeAfterSnapshot() {
  // Classes are registered before they are known to have been initialized, so
  // we have to delay setting the ids in the class objects or risk them being
  // overwritten. After all snapshot objects have been initialized, we can
  // correct the ids in the classes.
  for (intptr_t cid = kFirstLegalCid; cid < class_table_size_; cid++) {
    Behavior cls = static_cast<Behavior>(class_table_[cid]);
    cls->AssertCouldBeBehavior();
    if (cls->id() == interpreter_->nil_obj()) {
      cls->set_id(SmallInteger::New(cid));
    } else {
      ASSERT(cls->id() == SmallInteger::New(cid) ||
             cls->id() == SmallInteger::New(kEphemeronCid));
    }
    cls->set_in_class_table(true);
  }

  for (intptr_t i = 1; i < table_size_; i++) {
    HeapObject obj = table_[i];
    if (obj->IsRegularObject() || obj->IsEphemeron()) {
      Behavior klass = static_cast<Behavior>(class_table_[obj->cid()]);
      static_cast<RegularObject>(obj)->init_klass(klass);
    }
  }

#if defined(DEBUG)
  for (intptr_t cid = kFirstLegalCid; cid < class_table_size_; cid++) {
    Behavior cls = static_cast<Behavior>(class_table_[cid]);
    ASSERT(cls->incoming()->next != cls->incoming());
  }

  for (intptr_t i = 1; i < table_size_; i++) {
    HeapObject obj = table_[i];
    ASSERT(obj->incoming()->next != obj->incoming());
  }
#endif
}

intptr_t Heap::CountInstances(intptr_t cid) {
  intptr_t instances = 0;
  for (intptr_t i = 1; i < table_size_; i++) {
    HeapObject obj = table_[i];
    if (obj->cid() == cid) {
      instances++;
    }
  }
  return instances;
}

intptr_t Heap::CollectInstances(intptr_t cid, Array array) {
  intptr_t instances = 0;
  for (intptr_t i = 1; i < table_size_; i++) {
    HeapObject obj = table_[i];
    if (obj->cid() == cid) {
      array->init_element(instances, obj);
      instances++;
    }
  }
  return instances;
}

}  // namespace psoup
