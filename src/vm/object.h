// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_OBJECT_H_
#define VM_OBJECT_H_

#include "vm/assert.h"
#include "vm/globals.h"
#include "vm/bitfield.h"
#include "vm/utils.h"

namespace psoup {

enum PointerBits {
  kSmiTag = 0,
  kHeapObjectTag = 1,
  kSmiTagSize = 1,
  kSmiTagMask = 1,
  kSmiTagShift = 1,
};

enum ObjectAlignment {
  // Alignment offsets are used to determine object age.
  kNewObjectAlignmentOffset = kWordSize,
  kOldObjectAlignmentOffset = 0,
  // Object sizes are aligned to kObjectAlignment.
  kObjectAlignment = 2 * kWordSize,
  kObjectAlignmentLog2 = kWordSizeLog2 + 1,
  kObjectAlignmentMask = kObjectAlignment - 1,
};

enum HeaderBits {
  // During a scavenge: Has this object been copied to to-space?
  kMarkBit = 0,

  // Added to the remembered set. (as-yet-unused)
  kRememberedBit = 1,

  // For symbols.
  kCanonicalBit = 2,

#if defined(ARCH_IS_32_BIT)
  kSizeFieldOffset = 8,
  kSizeFieldSize = 8,
  kClassIdFieldOffset = 16,
  kClassIdFieldSize = 16,
#elif defined(ARCH_IS_64_BIT)
  kSizeFieldOffset = 16,
  kSizeFieldSize = 16,
  kClassIdFieldOffset = 32,
  kClassIdFieldSize = 32,
#endif
};

enum ClassIds {
  kIllegalCid = 0,
  kForwardingCorpseCid = 1,
  kFreeListElementCid = 2,

  kFirstLegalCid = 3,

  kSmiCid = 3,
  kMintCid = 4,
  kBigintCid = 5,
  kFloat64Cid = 6,
  kByteArrayCid = 7,
  kStringCid = 8,
  kArrayCid = 9,
  kWeakArrayCid = 10,
  kEphemeronCid = 11,
  kActivationCid = 12,
  kClosureCid = 13,

  kFirstRegularObjectCid = 14,
};

class AbstractMixin;
class Activation;
class Array;
class Behavior;
class ByteArray;
class String;
class Closure;
class Ephemeron;
class Heap;
class Isolate;
class Method;
class Object;
class SmallInteger;
class WeakArray;

#define HEAP_OBJECT_IMPLEMENTATION(klass)                                      \
 private:                                                                      \
  const klass* ptr() const {                                                   \
    ASSERT(IsHeapObject());                                                    \
    return reinterpret_cast<const klass*>(                                     \
        reinterpret_cast<uword>(this) - kHeapObjectTag);                       \
  }                                                                            \
  klass* ptr() {                                                               \
    ASSERT(IsHeapObject());                                                    \
    return reinterpret_cast<klass*>(                                           \
        reinterpret_cast<uword>(this) - kHeapObjectTag);                       \
  }                                                                            \
  friend class Object;                                                         \
 public:                                                                       \
  static const klass* Cast(const Object* object) {                             \
    return static_cast<const klass*>(object);                                  \
  }                                                                            \
  static klass* Cast(Object* object) {                                         \
    return static_cast<klass*>(object);                                        \
  }                                                                            \

class Object {
 public:
  bool IsObject() const {
    return true;
  }
  bool IsForwardingCorpse() const {
    return IsHeapObject() && (cid() == kForwardingCorpseCid);
  }
  bool IsFreeListElement() const {
    return IsHeapObject() && (cid() == kFreeListElementCid);
  }
  bool IsArray() const {
    return IsHeapObject() && (cid() == kArrayCid);
  }
  bool IsBytes() const {
    return IsHeapObject() && (cid() == kByteArrayCid || cid() == kStringCid);
  }
  bool IsByteArray() const {
    return IsHeapObject() && (cid() == kByteArrayCid);
  }
  bool IsString() const {
    return IsHeapObject() && (cid() == kStringCid);
  }
  bool IsActivation() const {
    return IsHeapObject() && (cid() == kActivationCid);
  }
  bool IsMediumInteger() const {
    return IsHeapObject() && (cid() == kMintCid);
  }
  bool IsLargeInteger() const {
    return IsHeapObject() && (cid() == kBigintCid);
  }
  bool IsFloat64() const {
    return IsHeapObject() && (cid() == kFloat64Cid);
  }
  bool IsWeakArray() const {
    return IsHeapObject() && (cid() == kWeakArrayCid);
  }
  bool IsEphemeron() const {
    return IsHeapObject() && (cid() == kEphemeronCid);
  }
  bool IsClosure() const {
    return IsHeapObject() && (cid() == kClosureCid);
  }
  bool IsRegularObject() const {
    return IsHeapObject() && (cid() >= kFirstRegularObjectCid);
  }
  bool IsHeapObject() const {
    return (reinterpret_cast<uword>(this) & kSmiTagMask) == kHeapObjectTag;
  }
  bool IsImmediateObject() const { return IsSmallInteger(); }
  bool IsSmallInteger() const {
    return (reinterpret_cast<uword>(this) & kSmiTagMask) == kSmiTag;
  }
  bool IsOldObject() const {
    ASSERT(IsHeapObject());
    uword addr = reinterpret_cast<uword>(this);
    return (addr & kNewObjectAlignmentOffset) == kOldObjectAlignmentOffset;
  }
  bool IsNewObject() const {
    ASSERT(IsHeapObject());
    uword addr = reinterpret_cast<uword>(this);
    return (addr & kNewObjectAlignmentOffset) == kNewObjectAlignmentOffset;
  }
  // Like !IsHeapObject() || IsOldObject(), but compiles to a single branch.
  bool IsImmediateOrOldObject() const {
    COMPILE_ASSERT(kHeapObjectTag == 1);
    COMPILE_ASSERT(kNewObjectAlignmentOffset == kWordSize);
    static const uword kNewObjectBits =
        (kNewObjectAlignmentOffset | kHeapObjectTag);
    const uword addr = reinterpret_cast<uword>(this);
    return (addr & kNewObjectBits) != kNewObjectBits;
  }

  void AssertCouldBeBehavior() const {
    ASSERT(IsRegularObject());
    // 8 slots for a class, 7 slots for a metaclass, plus 1 header.
    intptr_t heap_slots = heap_size() / sizeof(uword);
    ASSERT((heap_slots == 8) || (heap_slots == 10));
  }

  bool is_marked() const {
    return MarkBit::decode(ptr()->header_);
  }
  void set_is_marked(bool value) {
    ptr()->header_ = MarkBit::update(value, ptr()->header_);
  }
  bool is_canonical() const {
    return CanonicalBit::decode(ptr()->header_);
  }
  void set_is_canonical(bool value) {
    ptr()->header_ = CanonicalBit::update(value, ptr()->header_);
  }
  intptr_t heap_size() const {
    return SizeField::decode(ptr()->header_) << kObjectAlignmentLog2;
  }
  intptr_t cid() const {
    return ClassIdField::decode(ptr()->header_);
  }
  void set_cid(intptr_t value) {
    ptr()->header_ = ClassIdField::update(value, ptr()->header_);
  }
  intptr_t header_hash() const {
    return ptr()->header_hash_;
  }
  void set_header_hash(intptr_t value) {
    ptr()->header_hash_ = value;
  }

  uword Addr() const {
    return reinterpret_cast<uword>(this) - kHeapObjectTag;
  }
  static Object* FromAddr(uword addr) {
    return reinterpret_cast<Object*>(addr + kHeapObjectTag);
  }

  static Object* InitializeObject(uword addr,
                                  intptr_t cid,
                                  intptr_t heap_size) {
    ASSERT(cid != kIllegalCid);
    ASSERT((heap_size & kObjectAlignmentMask) == 0);
    ASSERT(heap_size > 0);
    intptr_t size_tag = heap_size >> kObjectAlignmentLog2;
    if (!SizeField::is_valid(size_tag)) {
      size_tag = 0;
      ASSERT(cid < kFirstRegularObjectCid);
    }
    uword header = 0;
    header = SizeField::update(size_tag, header);
    header = ClassIdField::update(cid, header);
    Object* obj = FromAddr(addr);
    obj->ptr()->header_ = header;
    obj->ptr()->header_hash_ = 0;
    ASSERT(obj->cid() == cid);
    ASSERT(!obj->is_marked());
    return obj;
  }

  intptr_t ClassId() const {
    if (IsSmallInteger()) {
      return kSmiCid;
    } else {
      return cid();
    }
  }

  Behavior* Klass(Heap* heap) const;

  intptr_t HeapSize() const {
    ASSERT(IsHeapObject());
    intptr_t heap_size_from_tag = heap_size();
    if (heap_size_from_tag != 0) {
      return heap_size_from_tag;
    }
    return HeapSizeFromClass();
  }
  intptr_t HeapSizeFromClass() const;
  void Pointers(Object*** from, Object*** to);

  char* ToCString(Heap* heap) const;
  void Print(Heap* heap) const;

 protected:
  template<typename type>
  void StorePointer(type* addr, type value) {
    *addr = value;
  }

 private:
  const Object* ptr() const {
    ASSERT(IsHeapObject());
    return reinterpret_cast<Object*>(
        reinterpret_cast<uword>(this) - kHeapObjectTag);
  }
  Object* ptr() {
    ASSERT(IsHeapObject());
    return reinterpret_cast<Object*>(
        reinterpret_cast<uword>(this) - kHeapObjectTag);
  }

  uword header_;
  uword header_hash_;

  class MarkBit : public BitField<bool, kMarkBit, 1> {};
  class CanonicalBit : public BitField<bool, kCanonicalBit, 1> {};
  class SizeField :
      public BitField<intptr_t, kSizeFieldOffset, kSizeFieldSize> {};
  class ClassIdField :
      public BitField<intptr_t, kClassIdFieldOffset, kClassIdFieldSize> {};
};


class ForwardingCorpse {
 public:
  Object* target() const {
    return ptr()->target_;
  }
  void set_target(Object* value) {
    ptr()->target_ = value;
  }
  intptr_t overflow_size() const {
    return ptr()->overflow_size_;
  }
  void set_overflow_size(intptr_t value) {
    ptr()->overflow_size_ = value;
  }

 protected:
  uword use_header() { return header_; }

 private:
  ForwardingCorpse* ptr() const {
    return reinterpret_cast<ForwardingCorpse*>(
        reinterpret_cast<uword>(this) - kHeapObjectTag);
  }

  uword header_;
  Object* target_;
  intptr_t overflow_size_;
};


class SmallInteger : public Object {
 public:
  static const intptr_t kBits = kBitsPerWord - 2;
  static const intptr_t kMaxValue = (static_cast<intptr_t>(1) << kBits) - 1;
  static const intptr_t kMinValue = -(static_cast<intptr_t>(1) << kBits);

  static SmallInteger* New(intptr_t value) {
    return reinterpret_cast<SmallInteger*>(value << kSmiTagShift);
  }

  intptr_t value() const {
    ASSERT(IsSmallInteger());
    return reinterpret_cast<intptr_t>(this) >> kSmiTagShift;
  }

  static bool IsSmiValue(int64_t value) {
    return (value >= static_cast<int64_t>(kMinValue)) &&
           (value <= static_cast<int64_t>(kMaxValue));
  }

#if defined(ARCH_IS_32_BIT)
  static bool IsSmiValue(intptr_t value) {
    // Check if the top two bits are equal.
    ASSERT(kSmiTagShift == 1);
    return (value ^ (value << 1)) >= 0;
  }
#endif
};


class MediumInteger : public Object {
  HEAP_OBJECT_IMPLEMENTATION(MediumInteger);

 public:
  static const int64_t kMinValue = kMinInt64;
  static const int64_t kMaxValue = kMaxInt64;

  int64_t value() const {
    return ptr()->value_;
  }
  void set_value(int64_t value) {
    ptr()->value_ = value;
  }

 private:
  int64_t value_;
};


#if defined(ARCH_IS_32_BIT)
typedef uint16_t digit_t;
typedef uint32_t ddigit_t;
typedef int32_t sddigit_t;
#elif defined(ARCH_IS_64_BIT)
typedef uint32_t digit_t;
typedef uint64_t ddigit_t;
typedef int64_t sddigit_t;
#endif
const ddigit_t kDigitBits = sizeof(digit_t) * kBitsPerByte;
const ddigit_t kDigitShift = sizeof(digit_t) * kBitsPerByte;
const ddigit_t kDigitBase = static_cast<ddigit_t>(1) << kDigitBits;
const ddigit_t kDigitMask = kDigitBase - static_cast<ddigit_t>(1);

class LargeInteger : public Object {
  HEAP_OBJECT_IMPLEMENTATION(LargeInteger);

 public:
  enum DivOperationType {
    kTruncated,
    kFloored,
    kExact,
  };

  enum DivResultType {
    kQuoitent,
    kRemainder
  };

  static LargeInteger* Expand(Object* integer, Heap* H);
  static Object* Reduce(LargeInteger* integer, Heap* H);

  static intptr_t Compare(LargeInteger* left, LargeInteger* right);

  static LargeInteger* Add(LargeInteger* left,
                           LargeInteger* right, Heap* H);
  static LargeInteger* Subtract(LargeInteger* left,
                                LargeInteger* right, Heap* H);
  static LargeInteger* Multiply(LargeInteger* left,
                                LargeInteger* right, Heap* H);
  static LargeInteger* Divide(DivOperationType op_type,
                              DivResultType result_type,
                              LargeInteger* left,
                              LargeInteger* right,
                              Heap* H);

  static LargeInteger* And(LargeInteger* left,
                           LargeInteger* right, Heap* H);
  static LargeInteger* Or(LargeInteger* left,
                          LargeInteger* right, Heap* H);
  static LargeInteger* Xor(LargeInteger* left,
                           LargeInteger* right, Heap* H);
  static LargeInteger* ShiftRight(LargeInteger* left,
                                  intptr_t raw_right, Heap* H);
  static LargeInteger* ShiftLeft(LargeInteger* left,
                                 intptr_t raw_right, Heap* H);

  static String* PrintString(LargeInteger* large, Heap* H);

  static double AsDouble(LargeInteger* integer);
  static bool FromDouble(double raw_value, Object** result, Heap* H);

  bool negative() const {
    return ptr()->negative_;
  }
  void set_negative(bool v) {
    ptr()->negative_ = v;
  }
  intptr_t size() const {
    return ptr()->size_;
  }
  void set_size(intptr_t value) {
    ptr()->size_ = value;
  }
  intptr_t capacity() const {
    return ptr()->capacity_;
  }
  void set_capacity(intptr_t value) {
    ptr()->capacity_ = value;
  }
  digit_t digit(intptr_t index) const {
    return ptr()->digits_[index];
  }
  void set_digit(intptr_t index, digit_t value) {
    ptr()->digits_[index] = value;
  }

 private:
  intptr_t capacity_;
  intptr_t negative_;  // TODO(rmacnak): Use a header bit?
  intptr_t size_;
  digit_t digits_[];
};


class RegularObject : public Object {
  HEAP_OBJECT_IMPLEMENTATION(RegularObject);

 public:
  Object* slot(intptr_t index) const {
    return ptr()->slots_[index];
  }
  void set_slot(intptr_t index, Object* value) {
    StorePointer(&ptr()->slots_[index], value);
  }

 private:
  Object** from() {
    return &ptr()->slots_[0];
  }
  Object* slots_[];
  Object** to() {
    intptr_t num_slots = (heap_size() - sizeof(Object)) >> kWordSizeLog2;
    return &ptr()->slots_[num_slots - 1];
  }
};


class Array : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Array);

 public:
  SmallInteger* size() const {
    return ptr()->size_;
  }
  void set_size(SmallInteger* s) {
    ptr()->size_ = s;
  }
  Object* element(intptr_t index) const {
    return ptr()->elements_[index];
  }
  void set_element(intptr_t index, Object* value) {
    StorePointer(&ptr()->elements_[index], value);
  }
  Object** element_addr(intptr_t index) {
    return &ptr()->elements_[index];
  }
  Object* const* element_addr(intptr_t index) const {
    return &ptr()->elements_[index];
  }

  intptr_t Size() const {
    return size()->value();
  }

 private:
  Object** from() {
    return &ptr()->elements_[0];
  }
  SmallInteger* size_;
  Object* elements_[];
  Object** to() {
    return &ptr()->elements_[Size() - 1];
  }
};


class WeakArray : public Object {
  HEAP_OBJECT_IMPLEMENTATION(WeakArray);

 public:
  SmallInteger* size() const {
    return ptr()->size_;
  }
  void set_size(SmallInteger* s) {
    ptr()->size_ = s;
  }
  WeakArray* next() const {
    return ptr()->next_;
  }
  void set_next(WeakArray* value) {
    ptr()->next_ = value;
  }
  Object* element(intptr_t index) const {
    return ptr()->elements_[index];
  }
  void set_element(intptr_t index, Object* value) {
    StorePointer(&ptr()->elements_[index], value);
  }

  intptr_t Size() const {
    return size()->value();
  }

 private:
  Object** from() {
    return &ptr()->elements_[0];
  }
  SmallInteger* size_;  // Not visited.
  WeakArray* next_;  // Not visited.
  Object* elements_[];
  Object** to() {
    return &ptr()->elements_[Size() - 1];
  }
};


class Ephemeron : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Ephemeron);

 public:
  Object* key() const { return ptr()->key_; }
  Object** key_ptr() { return &ptr()->key_; }
  void set_key(Object* key) {
    StorePointer(&ptr()->key_, key);
  }

  Object* value() const { return ptr()->value_; }
  Object** value_ptr() { return &ptr()->value_; }
  void set_value(Object* value) {
    StorePointer(&ptr()->value_, value);
  }

  Object* finalizer() const { return ptr()->finalizer_; }
  Object** finalizer_ptr() { return &ptr()->finalizer_; }
  void set_finalizer(Object* finalizer) {
    StorePointer(&ptr()->finalizer_, finalizer);
  }

  Ephemeron* next() const { return ptr()->next_; }
  void set_next(Ephemeron* value) { ptr()->next_ = value; }

 private:
  Object** from() {
    return &ptr()->key_;
  }
  Object* key_;
  Object* value_;
  Object* finalizer_;
  Ephemeron* next_;  // Not visited.
  Object** to() {
    return &ptr()->finalizer_;
  }
};

class Bytes : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Bytes);

 public:
  SmallInteger* size() const {
    return ptr()->size_;
  }
  void set_size(SmallInteger* size) {
    ptr()->size_ = size;
  }
  intptr_t Size() const {
    return size()->value();
  }
  uint8_t element(intptr_t index) const {
    return *element_addr(index);
  }
  void set_element(intptr_t index, uint8_t value) {
    *element_addr(index) = value;
  }
  uint8_t* element_addr(intptr_t index) {
    uint8_t* elements = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uword>(ptr()) + sizeof(Bytes));
    return &elements[index];
  }
  const uint8_t* element_addr(intptr_t index) const {
    const uint8_t* elements = reinterpret_cast<const uint8_t*>(
        reinterpret_cast<uword>(ptr()) + sizeof(Bytes));
    return &elements[index];
  }

 private:
  SmallInteger* size_;
};

class String : public Bytes {
  HEAP_OBJECT_IMPLEMENTATION(String);

 public:
  SmallInteger* EnsureHash(Isolate* isolate);
};

class ByteArray : public Bytes {
  HEAP_OBJECT_IMPLEMENTATION(ByteArray);
};

class Activation : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Activation);

 public:
  static const intptr_t kMaxTemps = 35;

  Activation* sender() const {
    return ptr()->sender_;
  }
  void set_sender(Activation* s) {
    StorePointer(&ptr()->sender_, s);
  }
  SmallInteger* bci() const {
    return ptr()->bci_;
  }
  void set_bci(SmallInteger* i) {
    ptr()->bci_ = i;
  }
  Method* method() const {
    return ptr()->method_;
  }
  void set_method(Method* m) {
    StorePointer(&ptr()->method_, m);
  }
  Closure* closure() const {
    return ptr()->closure_;
  }
  void set_closure(Closure* m) {
    StorePointer(&ptr()->closure_, m);
  }
  Object* receiver() const {
    return ptr()->receiver_;
  }
  void set_receiver(Object* o) {
    StorePointer(&ptr()->receiver_, o);
  }
  intptr_t stack_depth() const {
    return ptr()->stack_depth_->value();
  }
  void set_stack_depth(SmallInteger* d) {
    ptr()->stack_depth_ = d;
  }
  Object* temp(intptr_t index) const {
    return ptr()->temps_[index];
  }
  void set_temp(intptr_t index, Object* o) {
    StorePointer(&ptr()->temps_[index], o);
  }


  Object* Pop() {
    ASSERT(stack_depth() > 0);
    Object* top = ptr()->temps_[stack_depth() - 1];
    set_stack_depth(SmallInteger::New(stack_depth() - 1));
    return top;
  }
  Object* Stack(intptr_t depth) const {
    ASSERT(depth >= 0);
    ASSERT(depth < stack_depth());
    return temp(stack_depth() - depth - 1);
  }
  void StackPut(intptr_t depth, Object* o) {
    ASSERT(depth >= 0);
    ASSERT(depth < stack_depth());
    set_temp(stack_depth() - depth - 1, o);
  }
  void PopNAndPush(intptr_t drop_count, Object* value) {
    ASSERT(drop_count >= 0);
    ASSERT(drop_count <= stack_depth());
    set_stack_depth(SmallInteger::New(stack_depth() - drop_count + 1));
    set_temp(stack_depth() - 1, value);
  }
  void Push(Object* value) {
    PopNAndPush(0, value);
  }
  void Drop(intptr_t drop_count) {
    ASSERT(drop_count >= 0);
    ASSERT(drop_count <= stack_depth());
    set_stack_depth(SmallInteger::New(stack_depth() - drop_count));
  }
  void Grow(intptr_t grow_count) {
    ASSERT(grow_count >= 0);
    ASSERT(stack_depth() + grow_count < kMaxTemps);
    set_stack_depth(SmallInteger::New(stack_depth() + grow_count));
  }

 private:
  Object** from() {
    return reinterpret_cast<Object**>(&ptr()->sender_);
  }
  Activation* sender_;
  SmallInteger* bci_;
  Method* method_;
  Closure* closure_;
  Object* receiver_;
  SmallInteger* stack_depth_;
  Object* temps_[kMaxTemps];
  Object** to() {
    return reinterpret_cast<Object**>(&ptr()->temps_[stack_depth() - 1]);
  }
};


class Float64 : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Float64);

 public:
  double value() const {
    return ptr()->value_;
  }
  void set_value(double v) {
    ptr()->value_ = v;
  }

 private:
  double value_;
};


class Closure : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Closure);

 public:
  intptr_t num_copied() const {
    return ptr()->num_copied_->value();
  }
  void set_num_copied(intptr_t v) {
    ptr()->num_copied_ = SmallInteger::New(v);
  }
  Activation* defining_activation() const {
    return ptr()->defining_activation_;
  }
  void set_defining_activation(Activation* a) {
    StorePointer(&ptr()->defining_activation_, a);
  }
  SmallInteger* initial_bci() const {
    return ptr()->initial_bci_;
  }
  void set_initial_bci(SmallInteger* bci) {
    ptr()->initial_bci_ = bci;
  }
  SmallInteger* num_args() const {
    return ptr()->num_args_;
  }
  void set_num_args(SmallInteger* num) {
    ptr()->num_args_ = num;
  }
  Object* copied(intptr_t index) const {
    return ptr()->copied_[index];
  }
  void set_copied(intptr_t index, Object* o) {
    StorePointer(&ptr()->copied_[index], o);
  }

 private:
  Object** from() {
    return reinterpret_cast<Object**>(&ptr()->num_copied_);
  }
  SmallInteger* num_copied_;
  Activation* defining_activation_;
  SmallInteger* initial_bci_;
  SmallInteger* num_args_;
  Object* copied_[];
  Object** to() {
    return reinterpret_cast<Object**>(&ptr()->copied_[num_copied() - 1]);
  }
};


// === Regular objects with known slot offsets ===


class Behavior : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Behavior);

 public:
  Behavior* superclass() const { return ptr()->superclass_; }
  Array* methods() const { return ptr()->methods_; }
  AbstractMixin* mixin() const { return ptr()->mixin_; }
  Object* enclosing_object() const { return ptr()->enclosing_object_; }
  SmallInteger* id() const { return ptr()->classid_; }
  void set_id(SmallInteger* id) { ptr()->classid_ = id; }
  SmallInteger* format() const { return ptr()->format_; }

 private:
  Behavior* superclass_;
  Array* methods_;
  Object* enclosing_object_;
  AbstractMixin* mixin_;
  SmallInteger* classid_;
  SmallInteger* format_;
};


class Class : public Behavior {
  HEAP_OBJECT_IMPLEMENTATION(Class);

 public:
  String* name() const { return ptr()->name_; }
  WeakArray* subclasses() const { return ptr()->subclasses_; }

 private:
  String* name_;
  WeakArray* subclasses_;
};


class Metaclass : public Behavior {
  HEAP_OBJECT_IMPLEMENTATION(Metaclass);

 public:
  Class* this_class() const { return ptr()->this_class_; }

 private:
  Class* this_class_;
};


class AbstractMixin : public Object {
  HEAP_OBJECT_IMPLEMENTATION(AbstractMixin);

 public:
  String* name() const { return ptr()->name_; }
  Array* methods() const { return ptr()->methods_; }
  AbstractMixin* enclosing_mixin() const { return ptr()->enclosing_mixin_; }

 private:
  String* name_;
  Array* methods_;
  AbstractMixin* enclosing_mixin_;
};


class Method : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Method);

 public:
  Array* literals() const { return ptr()->literals_; }
  ByteArray* bytecode() const { return ptr()->bytecode_; }
  AbstractMixin* mixin() const { return ptr()->mixin_; }
  String* selector() const { return ptr()->selector_; }
  Object* source() const { return ptr()->source_; }

  bool IsPublic() const {
    uword header = ptr()->header_->value();
    uword am = header >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 0;
  }
  bool IsProtected() const {
    uword header = ptr()->header_->value();
    uword am = header >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 1;
  }
  bool IsPrivate() const {
    uword header = ptr()->header_->value();
    uword am = header >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 2;
  }
  intptr_t Primitive() const {
    uword header = ptr()->header_->value();
    return (header >> 16) & 1023;
  }
  intptr_t NumArgs() const {
    uword header = ptr()->header_->value();
    return (header >> 0) & 255;
  }
  intptr_t NumTemps() const {
    uword header = ptr()->header_->value();
    return (header >> 8) & 255;
  }

 private:
  SmallInteger* header_;
  Array* literals_;
  ByteArray* bytecode_;
  AbstractMixin* mixin_;
  String* selector_;
  Object* source_;
};


class Message : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Message);

 public:
  void set_selector(String* selector) {
    StorePointer(&ptr()->selector_, selector);
  }
  void set_arguments(Array* arguments) {
    StorePointer(&ptr()->arguments_, arguments);
  }
 private:
  String* selector_;
  Array* arguments_;
};


class Scheduler : public Object {
  HEAP_OBJECT_IMPLEMENTATION(Scheduler);
};


class ObjectStore : public Object {
  HEAP_OBJECT_IMPLEMENTATION(ObjectStore);

 public:
  class SmallInteger* size() const { return ptr()->array_size_; }
  Object* nil_obj() const { return ptr()->nil_; }
  Object* false_obj() const { return ptr()->false_; }
  Object* true_obj() const { return ptr()->true_; }
  Scheduler* scheduler() const { return ptr()->scheduler_; }
  class Array* quick_selectors() const { return ptr()->quick_selectors_; }
  class String* does_not_understand() const {
    return ptr()->does_not_understand_;
  }
  class String* non_boolean_receiver() const {
    return ptr()->non_boolean_receiver_;
  }
  class String* cannot_return() const {
    return ptr()->cannot_return_;
  }
  class String* about_to_return_through() const {
    return ptr()->about_to_return_through_;
  }
  class String* unused_bytecode() const {
    return ptr()->unused_bytecode_;
  }
  class String* dispatch_message() const {
    return ptr()->dispatch_message_;
  }
  class String* dispatch_signal() const {
    return ptr()->dispatch_signal_;
  }
  Behavior* Array() const { return ptr()->Array_; }
  Behavior* ByteArray() const { return ptr()->ByteArray_; }
  Behavior* String() const { return ptr()->String_; }
  Behavior* Closure() const { return ptr()->Closure_; }
  Behavior* Ephemeron() const { return ptr()->Ephemeron_; }
  Behavior* Float64() const { return ptr()->Float64_; }
  Behavior* LargeInteger() const { return ptr()->LargeInteger_; }
  Behavior* MediumInteger() const { return ptr()->MediumInteger_; }
  Behavior* Message() const { return ptr()->Message_; }
  Behavior* SmallInteger() const { return ptr()->SmallInteger_; }
  Behavior* WeakArray() const { return ptr()->WeakArray_; }
  Behavior* Activation() const { return ptr()->Activation_; }
  Behavior* Method() const { return ptr()->Method_; }

 private:
  class SmallInteger* array_size_;
  Object* nil_;
  Object* false_;
  Object* true_;
  Scheduler* scheduler_;
  class Array* quick_selectors_;
  class String* does_not_understand_;
  class String* non_boolean_receiver_;
  class String* cannot_return_;
  class String* about_to_return_through_;
  class String* unused_bytecode_;
  class String* dispatch_message_;
  class String* dispatch_signal_;
  Behavior* Array_;
  Behavior* ByteArray_;
  Behavior* String_;
  Behavior* Closure_;
  Behavior* Ephemeron_;
  Behavior* Float64_;
  Behavior* LargeInteger_;
  Behavior* MediumInteger_;
  Behavior* Message_;
  Behavior* SmallInteger_;
  Behavior* WeakArray_;
  Behavior* Activation_;
  Behavior* Method_;
};

}  // namespace psoup

#endif  // VM_OBJECT_H_
