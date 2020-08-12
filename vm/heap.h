// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_HEAP_H_
#define VM_HEAP_H_

#include "vm/assert.h"
#include "vm/flags.h"
#include "vm/globals.h"
#include "vm/object.h"
#include "vm/utils.h"
#include "vm/virtual_memory.h"

namespace psoup {

class Interpreter;

// Note these values are never valid Object.
#if defined(ARCH_IS_32_BIT)
static const uword kUnallocatedWord = 0xabababab;
static const uword kUninitializedWord = 0xcbcbcbcb;
#elif defined(ARCH_IS_64_BIT)
static const uword kUnallocatedWord = 0xabababababababab;
static const uword kUninitializedWord = 0xcbcbcbcbcbcbcbcb;
#endif
static const uint8_t kUnallocatedByte = 0xab;
static const uint8_t kUninitializedByte = 0xcb;

static intptr_t AllocationSize(intptr_t size) {
  return Utils::RoundUp(size, kObjectAlignment);
}

// C. J. Cheney. "A nonrecursive list compacting algorithm." Communications of
// the ACM. 1970.
//
// Barry Hayes. "Ephemerons: a New Finalization Mechanism." Object-Oriented
// Languages, Programming, Systems, and Applications. 1997.
class Heap {
 public:
  enum Allocator { kNormal, kSnapshot };

  enum GrowthPolicy { kControlGrowth, kForceGrowth };

  enum Reason {
    kNewSpace,
    kTenure,
    kOldSpace,
    kClassTable,
    kPrimitive,
    kSnapshotTest
  };

  static const char* ReasonToCString(Reason reason) {
    switch (reason) {
      case kNewSpace: return "new-space";
      case kTenure: return "tenure";
      case kOldSpace: return "old-space";
      case kClassTable: return "class-table";
      case kPrimitive: return "primitive";
      case kSnapshotTest: return "snapshot-test";
    }
    UNREACHABLE();
    return nullptr;
  }

  Heap();
  ~Heap();

  RegularObject AllocateRegularObject(intptr_t cid, intptr_t num_slots,
                                      Allocator allocator = kNormal) {
    ASSERT(cid == kEphemeronCid || cid >= kFirstRegularObjectCid);
    const intptr_t heap_size =
        AllocationSize(num_slots * sizeof(Ref) + sizeof(RegularObject::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, cid, heap_size);
    RegisterInstance(obj);
    RegularObject result = static_cast<RegularObject>(obj);
    ASSERT(result->IsRegularObject() || result->IsEphemeron());
    ASSERT(result->HeapSize() == heap_size);
    if (allocator != kSnapshot) {
      static_cast<Behavior>(class_table_[cid])->AssertCouldBeBehavior();
      result->init_klass(static_cast<Behavior>(class_table_[cid]));
    }
    return result;
  }

  ByteArray AllocateByteArray(intptr_t num_bytes,
                              Allocator allocator = kNormal) {
    const intptr_t heap_size =
        AllocationSize(num_bytes * sizeof(uint8_t) + sizeof(ByteArray::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kByteArrayCid, heap_size);
    RegisterInstance(obj);
    ByteArray result = static_cast<ByteArray>(obj);
    result->init_size(SmallInteger::New(num_bytes));
    ASSERT(result->IsByteArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  String AllocateString(intptr_t num_bytes, Allocator allocator = kNormal) {
    const intptr_t heap_size =
        AllocationSize(num_bytes * sizeof(uint8_t) + sizeof(String::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kStringCid, heap_size);
    RegisterInstance(obj);
    String result = static_cast<String>(obj);
    result->init_size(SmallInteger::New(num_bytes));
    ASSERT(result->IsString());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Array AllocateArray(intptr_t num_slots, Allocator allocator = kNormal) {
    const intptr_t heap_size =
        AllocationSize(num_slots * sizeof(Ref) + sizeof(Array::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kArrayCid, heap_size);
    RegisterInstance(obj);
    Array result = static_cast<Array>(obj);
    result->init_size(SmallInteger::New(num_slots));
    ASSERT(result->IsArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  WeakArray AllocateWeakArray(intptr_t num_slots,
                              Allocator allocator = kNormal) {
    const intptr_t heap_size =
        AllocationSize(num_slots * sizeof(Ref) + sizeof(WeakArray::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kWeakArrayCid, heap_size);
    RegisterInstance(obj);
    WeakArray result = static_cast<WeakArray>(obj);
    result->init_size(SmallInteger::New(num_slots));
    ASSERT(result->IsWeakArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Closure AllocateClosure(intptr_t num_copied, Allocator allocator = kNormal) {
    const intptr_t heap_size =
        AllocationSize(num_copied * sizeof(Ref) + sizeof(Closure::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kClosureCid, heap_size);
    RegisterInstance(obj);
    Closure result = static_cast<Closure>(obj);
    result->init_num_copied(SmallInteger::New(num_copied));
    ASSERT(result->IsClosure());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Activation AllocateActivation(Allocator allocator = kNormal) {
    const intptr_t heap_size = AllocationSize(sizeof(Activation::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kActivationCid, heap_size);
    RegisterInstance(obj);
    Activation result = static_cast<Activation>(obj);
    ASSERT(result->IsActivation());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  MediumInteger AllocateMediumInteger(Allocator allocator = kNormal) {
    const intptr_t heap_size = AllocationSize(sizeof(MediumInteger::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kMintCid, heap_size);
    RegisterInstance(obj);
    MediumInteger result = static_cast<MediumInteger>(obj);
    ASSERT(result->IsMediumInteger());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  LargeInteger AllocateLargeInteger(intptr_t capacity,
                                    Allocator allocator = kNormal) {
    const intptr_t heap_size = AllocationSize(capacity * sizeof(digit_t) +
                                              sizeof(LargeInteger::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kBigintCid, heap_size);
    RegisterInstance(obj);
    LargeInteger result = static_cast<LargeInteger>(obj);
    result->set_capacity(capacity);
    ASSERT(result->IsLargeInteger());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Float64 AllocateFloat64(Allocator allocator = kNormal) {
    const intptr_t heap_size = AllocationSize(sizeof(Float64::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kFloat64Cid, heap_size);
    RegisterInstance(obj);
    Float64 result = static_cast<Float64>(obj);
    ASSERT(result->IsFloat64());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Message AllocateMessage();

  size_t Size() const {
    return heap_size_;
  }

  void CollectAll(Reason reason);

  intptr_t CountInstances(intptr_t cid);
  intptr_t CollectInstances(intptr_t cid, Array array);

  bool BecomeForward(Array old, Array neu);

  intptr_t AllocateClassId();
  void RegisterClass(intptr_t cid, Behavior cls) {
    ASSERT(class_table_[cid] == static_cast<Object>(kUninitializedWord));
    class_table_[cid] = cls;
    cls->set_id(SmallInteger::New(cid));
    cls->AssertCouldBeBehavior();
    ASSERT(cls->cid() >= kFirstRegularObjectCid);
    cls->set_in_class_table(true);
  }
  Behavior ClassAt(intptr_t cid) const {
    ASSERT(cid > kIllegalCid);
    ASSERT(cid < class_table_size_);
    return static_cast<Behavior>(class_table_[cid]);
  }
  void RegisterInstance(HeapObject obj) {
    if (table_size_ == table_capacity_) {
      TableGrow();
    }
    intptr_t index = table_size_++;
    table_[index] = obj;
    obj->set_table_index(index);
  }

  void InitializeInterpreter(Interpreter* interpreter) {
    ASSERT(interpreter_ == nullptr);
    interpreter_ = interpreter;
  }
  void InitializeAfterSnapshot();

  Interpreter* interpreter() const { return interpreter_; }

  intptr_t handles() const { return handles_size_; }
  void set_handles(intptr_t value) { handles_size_ = value; }

 private:
  void Verify();
  void GCStep();
  bool CheckReachable(HeapObject obj);
  void WorklistPush(HeapObject obj);
  void TableGrow();
  void Unlink(HeapObject obj);
  void Free(HeapObject obj);

  // Become.
  void ForwardClassIds();
  void ForwardRoots();
  void ForwardHeap();
  void MournClassTableForwarded();

  uword Allocate(intptr_t size, Allocator allocator);

  size_t heap_size_;

  // Class table.
  Object* class_table_;
  intptr_t class_table_size_;
  intptr_t class_table_capacity_;
  intptr_t class_table_free_;

  HeapObject* worklist_;
  intptr_t worklist_size_;
  intptr_t worklist_capacity_;
  HeapObject* table_;
  intptr_t table_size_;
  intptr_t table_capacity_;

  // Roots.
  Interpreter* interpreter_;
  static const intptr_t kHandlesCapacity = 8;
  Object* handles_[kHandlesCapacity];
  intptr_t handles_size_;
  friend class HandleScope;

  int64_t max_gc_time_;
  int64_t total_gc_time_;
  intptr_t gc_count_;

  DISALLOW_COPY_AND_ASSIGN(Heap);
};


class HandleScope {
 public:
  HandleScope(Heap* heap, Object* ptr) : heap_(heap) {
    ASSERT(heap_->handles_size_ < Heap::kHandlesCapacity);
    heap_->handles_[heap_->handles_size_++] = ptr;
  }

  ~HandleScope() {
    heap_->handles_size_--;
  }

 private:
  Heap* heap_;
};

}  // namespace psoup

#endif  // VM_HEAP_H_
