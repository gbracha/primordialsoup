// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_OBJECT_H_
#define VM_OBJECT_H_

#include "vm/assert.h"
#include "vm/globals.h"
#include "vm/bitfield.h"
#include "vm/utils.h"

namespace psoup {

INLINE static const uint8_t* Unhide(const uint8_t* ip) {
  return (const uint8_t*)(((uword)ip)>>1);
}
INLINE static const uint8_t* Hide(const uint8_t* ip) {
  ASSERT((uword)ip<<1>>1 == (uword)ip);
  return (const uint8_t*)(((uword)ip)<<1);
}

enum PointerBits {
  kSmiTag = 0,
  kHeapObjectTag = 1,
  kSmiTagSize = 1,
  kSmiTagMask = 1,
  kSmiTagShift = 1,
};

enum ObjectAlignment {
  // Object sizes are aligned to kObjectAlignment.
  kObjectAlignment = 4 * kWordSize,
  kObjectAlignmentLog2 = kWordSizeLog2 + 1,
  kObjectAlignmentMask = kObjectAlignment - 1,
};

enum HeaderBits {
  // In the backtracing worklist.
  kMarkBit = 0,

  // Saw a WeakArray pointing to this.
  kWeakReferentBit = 1,

  // Registered in the class table.
  kInClassTableBit = 2,

  // For symbols.
  kCanonicalBit = 3,

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

class Heap;
class Isolate;

class AbstractMixin;
class Activation;
class Array;
class Behavior;
class ByteArray;
class Closure;
class Ephemeron;
class Method;
class Object;
class SmallInteger;
class String;
class WeakArray;

#define HEAP_OBJECT_IMPLEMENTATION(klass, base)                                \
 public:                                                                       \
  class Layout;                                                                \
  explicit constexpr klass() : base() {}                                       \
  explicit constexpr klass(uword tagged) : base(tagged) {}                     \
  explicit constexpr klass(const Object& obj) : base(obj) {}                   \
  constexpr klass(nullptr_t) : base(nullptr) {}                                \
  klass* operator ->() { return this; }                                        \
  const klass* operator ->() const { return this; }                            \
  static klass Cast(const Object& object) {                                    \
    return static_cast<klass>(object);                                         \
  }                                                                            \
 private:                                                                      \
  const Layout* ptr() const {                                                  \
    ASSERT(IsHeapObject());                                                    \
    return reinterpret_cast<const Layout*>(Addr());                            \
  }                                                                            \
  Layout* ptr() {                                                              \
    ASSERT(IsHeapObject());                                                    \
    return reinterpret_cast<Layout*>(Addr());                                  \
  }                                                                            \

class Object {
 public:
  bool IsForwardingCorpse() const { return ClassId() == kForwardingCorpseCid; }
  bool IsFreeListElement() const { return ClassId() == kFreeListElementCid; }
  bool IsArray() const { return ClassId() == kArrayCid; }
  bool IsByteArray() const { return ClassId() == kByteArrayCid; }
  bool IsString() const { return ClassId() == kStringCid; }
  bool IsActivation() const { return ClassId() == kActivationCid; }
  bool IsMediumInteger() const { return ClassId() == kMintCid; }
  bool IsLargeInteger() const { return ClassId() == kBigintCid; }
  bool IsFloat64() const { return ClassId() == kFloat64Cid; }
  bool IsWeakArray() const { return ClassId() == kWeakArrayCid; }
  bool IsEphemeron() const { return ClassId() == kEphemeronCid; }
  bool IsClosure() const { return ClassId() == kClosureCid; }
  bool IsRegularObject() const { return ClassId() >= kFirstRegularObjectCid; }
  bool IsBytes() const {
    return (ClassId() == kByteArrayCid) || (ClassId() == kStringCid);
  }

  bool IsHeapObject() const {
    return (tagged_pointer_ & kSmiTagMask) == kHeapObjectTag;
  }
  bool IsImmediateObject() const { return IsSmallInteger(); }
  bool IsSmallInteger() const {
    return (tagged_pointer_ & kSmiTagMask) == kSmiTag;
  }

  inline intptr_t ClassId() const;
  Behavior Klass(Heap* heap) const;

  char* ToCString(Heap* heap) const;
  void Print(Heap* heap) const;

 public:
  constexpr Object() : tagged_pointer_(0) {}
  explicit constexpr Object(uword tagged) : tagged_pointer_(tagged) {}
  constexpr Object(nullptr_t) : tagged_pointer_(0) {}  // NOLINT

  Object* operator->() { return this; }

  constexpr bool operator ==(const Object& other) const {
    return tagged_pointer_ == other.tagged_pointer_;
  }
  constexpr bool operator !=(const Object& other) const {
    return tagged_pointer_ != other.tagged_pointer_;
  }
  constexpr bool operator ==(nullptr_t other) const {
    return tagged_pointer_ == 0;
  }
  constexpr bool operator !=(nullptr_t other) const {
    return tagged_pointer_ != 0;
  }

  operator bool() const = delete;
  explicit operator uword() const {
    return tagged_pointer_;
  }
  explicit operator intptr_t() const {
    return static_cast<intptr_t>(tagged_pointer_);
  }

 protected:
  uword tagged_pointer_;
};

class Link {
 public:
  Link* prev;
  Link* next;

  void Init() {
    prev = this;
    next = this;
  }

  bool IsEmpty() const {
    return next == this;
  }

  void Insert(Link* new_link) {
    ASSERT(new_link->next == new_link);
    ASSERT(new_link->prev == new_link);

    Link* before = prev;
    Link* after = this;

    before->next = new_link;
    new_link->prev = before;

    after->prev = new_link;
    new_link->next = after;
  }

  void Remove() {
    ASSERT(next != this);
    ASSERT(prev != this);

    Link* before = prev;
    Link* after = next;
    before->next = after;
    after->prev = before;

#if defined(DEBUG)
    prev = this;
    next = this;
#endif
  }

  void Poison() {
#if defined(DEBUG)
    prev = nullptr;
    next = nullptr;
#endif
  }
};

class Ref : public Link {
 public:
  Object from;
  Object to;

  inline void InitRoot(Object target);
  inline void Init(Object source, Object target);
  inline void Update(Object source, Object target);
  inline void UpdateNoCheck(Object target);
};

class HeapObject : public Object {
  HEAP_OBJECT_IMPLEMENTATION(HeapObject, Object);

 public:
  void AssertCouldBeBehavior() const {
    ASSERT(IsHeapObject());
    ASSERT(IsRegularObject());
    // 8 slots for a class, 7 slots for a metaclass, plus 1 header.
    intptr_t heap_slots = heap_size() / sizeof(Ref);
    ASSERT((heap_slots == 9) || (heap_slots == 10));
  }

  inline bool is_marked() const;
  inline void set_is_marked(bool value);
  inline bool is_weak_referent() const;
  inline void set_is_weak_referent(bool value);
  inline bool in_class_table() const;
  inline void set_in_class_table(bool value);
  inline bool is_canonical() const;
  inline void set_is_canonical(bool value);
  inline intptr_t heap_size() const;
  inline intptr_t cid() const;
  inline void set_cid(intptr_t value);
  inline intptr_t header_hash() const;
  inline void set_header_hash(intptr_t value);
  inline intptr_t table_index() const;
  inline void set_table_index(intptr_t value);
  inline Link* incoming();

  uword Addr() const {
    return tagged_pointer_ - kHeapObjectTag;
  }
  static HeapObject FromAddr(uword addr) {
    return HeapObject(addr + kHeapObjectTag);
  }

  inline static HeapObject Initialize(uword addr,
                                      intptr_t cid,
                                      intptr_t heap_size);

  intptr_t HeapSize() const {
    ASSERT(IsHeapObject());
    intptr_t heap_size_from_tag = heap_size();
    if (heap_size_from_tag != 0) {
      return heap_size_from_tag;
    }
    return HeapSizeFromClass();
  }
  intptr_t HeapSizeFromClass() const;
  void Pointers(Ref** from, Ref** to);

 protected:
  template<typename type>
  type Load(const Ref* addr) const {
    return static_cast<type>(const_cast<Ref*>(addr)->to);
  }

  template<typename type>
  void Store(Ref* addr, type value) {
    addr->Update(*this, value);
  }

  template<typename type>
  void Init(Ref* addr, type value) {
    addr->Init(*this, value);
  }

 private:
  class MarkBit : public BitField<bool, kMarkBit, 1> {};
  class WeakReferentBit : public BitField<bool, kWeakReferentBit, 1> {};
  class InClassTableBit : public BitField<bool, kInClassTableBit, 1> {};
  class CanonicalBit : public BitField<bool, kCanonicalBit, 1> {};
  class SizeField :
      public BitField<intptr_t, kSizeFieldOffset, kSizeFieldSize> {};
  class ClassIdField :
      public BitField<intptr_t, kClassIdFieldOffset, kClassIdFieldSize> {};

  class IndexField : public BitField<intptr_t, 0, 32> {};
  class HashField : public BitField<intptr_t, 32, 32> {};
};

intptr_t Object::ClassId() const {
  if (IsSmallInteger()) {
    return kSmiCid;
  } else {
    return static_cast<const HeapObject*>(this)->cid();
  }
}

class ForwardingCorpse : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(ForwardingCorpse, HeapObject);

 public:
  inline Object target() const;
  inline void set_target(Object value);
  inline intptr_t overflow_size() const;
  inline void set_overflow_size(intptr_t value);
};

class FreeListElement : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(FreeListElement, HeapObject);

 public:
  inline FreeListElement next() const;
  inline void set_next(FreeListElement value);
  inline intptr_t overflow_size() const;
  inline void set_overflow_size(intptr_t value);
};

class SmallInteger : public Object {
 public:
  static const intptr_t kBits = kBitsPerWord - 2;
  static const intptr_t kMaxValue = (static_cast<intptr_t>(1) << kBits) - 1;
  static const intptr_t kMinValue = -(static_cast<intptr_t>(1) << kBits);

  explicit constexpr SmallInteger(const Object& obj) : Object(obj) {}
  explicit constexpr SmallInteger(uword tagged) : Object(tagged) {}
  const SmallInteger* operator->() const { return this; }

  static SmallInteger New(intptr_t value) {
    return static_cast<SmallInteger>(
        static_cast<uintptr_t>(value) << kSmiTagShift);
  }

  intptr_t value() const {
    ASSERT(IsSmallInteger());
    return static_cast<intptr_t>(tagged_pointer_) >> kSmiTagShift;
  }

#if defined(ARCH_IS_32_BIT)
  static bool IsSmiValue(int64_t value) {
    return (value >= static_cast<int64_t>(kMinValue)) &&
           (value <= static_cast<int64_t>(kMaxValue));
  }
#endif

  static bool IsSmiValue(intptr_t value) {
    intptr_t tagged = static_cast<uintptr_t>(value) << kSmiTagShift;
    intptr_t untagged = tagged >> kSmiTagShift;
    return untagged == value;
  }
};

class MediumInteger : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(MediumInteger, HeapObject);

 public:
  static const int64_t kMinValue = kMinInt64;
  static const int64_t kMaxValue = kMaxInt64;

  inline int64_t value() const;
  inline void set_value(int64_t value);
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

class LargeInteger : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(LargeInteger, HeapObject);

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

  static LargeInteger Expand(Object integer, Heap* H);
  static Object Reduce(LargeInteger integer, Heap* H);

  static intptr_t Compare(LargeInteger left, LargeInteger right);

  static LargeInteger Add(LargeInteger left,
                          LargeInteger right, Heap* H);
  static LargeInteger Subtract(LargeInteger left,
                               LargeInteger right, Heap* H);
  static LargeInteger Multiply(LargeInteger left,
                               LargeInteger right, Heap* H);
  static LargeInteger Divide(DivOperationType op_type,
                             DivResultType result_type,
                             LargeInteger left,
                             LargeInteger right,
                             Heap* H);

  static LargeInteger And(LargeInteger left,
                          LargeInteger right, Heap* H);
  static LargeInteger Or(LargeInteger left,
                         LargeInteger right, Heap* H);
  static LargeInteger Xor(LargeInteger left,
                          LargeInteger right, Heap* H);
  static LargeInteger ShiftRight(LargeInteger left,
                                 intptr_t raw_right, Heap* H);
  static LargeInteger ShiftLeft(LargeInteger left,
                                intptr_t raw_right, Heap* H);

  static String PrintString(LargeInteger large, Heap* H);

  static double AsDouble(LargeInteger integer);
  static bool FromDouble(double raw_value, Object* result, Heap* H);

  inline bool negative() const;
  inline void set_negative(bool v);
  inline intptr_t size() const;
  inline void set_size(intptr_t value);
  inline intptr_t capacity() const;
  inline void set_capacity(intptr_t value);
  inline digit_t digit(intptr_t index) const;
  inline void set_digit(intptr_t index, digit_t value);
};

class RegularObject : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(RegularObject, HeapObject);

 public:
  inline void init_klass(Behavior value);
  inline void set_klass(Behavior value);
  inline Object slot(intptr_t index) const;
  inline void init_slot(intptr_t index, Object value);
  inline void set_slot(intptr_t index, Object value);

  inline Ref* from();
  inline Ref* to();
};

class Array : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Array, HeapObject);

 public:
  inline SmallInteger size() const;
  inline void set_size(SmallInteger s);
  inline void init_size(SmallInteger s);
  intptr_t Size() const { return size()->value(); }

  inline Object element(intptr_t index) const;
  inline void set_element(intptr_t index, Object value);
  inline void init_element(intptr_t index, Object value);

  inline Ref* from();
  inline Ref* to();
};

class WeakArray : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(WeakArray, HeapObject);

 public:
  inline SmallInteger size() const;
  inline void set_size(SmallInteger s);
  inline void init_size(SmallInteger s);
  intptr_t Size() const { return size()->value(); }

  // Only accessed by the GC. Bypasses barrier, including assertions.
  inline WeakArray next() const;
  inline void set_next(WeakArray value);

  inline Object element(intptr_t index) const;
  inline void set_element(intptr_t index, Object value);
  inline void init_element(intptr_t index, Object value);

  inline Ref* from();
  inline Ref* to();
};

class Ephemeron : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Ephemeron, HeapObject);

 public:
  inline Object key() const;
  inline void set_key(Object key);
  inline void init_key(Object key);

  inline Object value() const;
  inline void set_value(Object value);
  inline void init_value(Object value);

  inline Object finalizer() const;
  inline void set_finalizer(Object finalizer);
  inline void init_finalizer(Object finalizer);

  inline Ref* from();
  inline Ref* to();
};

class Bytes : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Bytes, HeapObject);

 public:
  inline SmallInteger size() const;
  inline void set_size(SmallInteger s);
  inline void init_size(SmallInteger s);
  intptr_t Size() const { return size()->value(); }

  inline uint8_t element(intptr_t index) const;
  inline void set_element(intptr_t index, uint8_t value);
  inline uint8_t* element_addr(intptr_t index);
  inline const uint8_t* element_addr(intptr_t index) const;
};

class String : public Bytes {
  HEAP_OBJECT_IMPLEMENTATION(String, Bytes);

 public:
  SmallInteger EnsureHash(Isolate* isolate);
};

class ByteArray : public Bytes {
  HEAP_OBJECT_IMPLEMENTATION(ByteArray, Bytes);
};

static const intptr_t kMaxTemps = 35;

class Activation : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Activation, HeapObject);

 public:
  inline Activation sender() const;
  inline void set_sender(Activation s);
  inline void init_sender(Activation s);
  Ref* sender_fp() const {
    return reinterpret_cast<Ref*>(static_cast<uword>(sender()));
  }
  void set_sender_fp(Ref* fp) {
    Activation sender = static_cast<Activation>(reinterpret_cast<uword>(fp));
    ASSERT(sender->IsSmallInteger());
    set_sender(sender);
  }
  void init_sender_fp(Ref* fp) {
    Activation sender = static_cast<Activation>(reinterpret_cast<uword>(fp));
    ASSERT(sender->IsSmallInteger());
    init_sender(sender);
  }

  inline SmallInteger bci() const;
  inline void set_bci(SmallInteger i);
  inline void init_bci(SmallInteger i);

  inline Method method() const;
  inline void set_method(Method m);
  inline void init_method(Method m);

  inline Closure closure() const;
  inline void set_closure(Closure m);
  inline void init_closure(Closure m);

  inline Object receiver() const;
  inline void set_receiver(Object o);
  inline void init_receiver(Object o);

  inline SmallInteger stack_depth() const;
  inline void set_stack_depth(SmallInteger d);
  inline void init_stack_depth(SmallInteger d);
  intptr_t StackDepth() const { return stack_depth()->value(); }

  inline Object temp(intptr_t index) const;
  inline void set_temp(intptr_t index, Object o);
  inline void init_temp(intptr_t index, Object o);

  void PopNAndPush(intptr_t drop_count, Object value) {
    ASSERT(drop_count >= 0);
    ASSERT(drop_count <= StackDepth());
    set_stack_depth(SmallInteger::New(StackDepth() - drop_count + 1));
    set_temp(StackDepth() - 1, value);
  }
  void Push(Object value) {
    PopNAndPush(0, value);
  }

  void PrintStack(Heap* heap);

  inline Ref* from();
  inline Ref* to();
};

class Method : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Method, HeapObject);

 public:
  inline SmallInteger header() const;
  inline Array literals() const;
  inline ByteArray bytecode() const;
  inline AbstractMixin mixin() const;
  inline String selector() const;
  inline Object source() const;

  bool IsPublic() const {
    uword am = header()->value() >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 0;
  }
  bool IsProtected() const {
    uword am = header()->value() >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 1;
  }
  bool IsPrivate() const {
    uword am = header()->value() >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 2;
  }
  intptr_t Primitive() const {
    return (header()->value() >> 16) & 1023;
  }
  intptr_t NumArgs() const {
    return (header()->value() >> 0) & 255;
  }
  intptr_t NumTemps() const {
    return (header()->value() >> 8) & 255;
  }

  const uint8_t* IP(const SmallInteger bci) {
    return Hide(bytecode()->element_addr(bci->value() - 1));
  }
  SmallInteger BCI(const uint8_t* ip) {
    return SmallInteger::New((Unhide(ip) - bytecode()->element_addr(0)) + 1);
  }
};

class Float64 : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Float64, HeapObject);

 public:
  inline double value() const;
  inline void set_value(double v);
};

class Closure : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Closure, HeapObject);

 public:
  inline SmallInteger num_copied() const;
  inline void set_num_copied(SmallInteger v);
  inline void init_num_copied(SmallInteger v);
  intptr_t NumCopied() const { return num_copied()->value(); }

  inline Activation defining_activation() const;
  inline void set_defining_activation(Activation a);
  inline void init_defining_activation(Activation a);

  inline SmallInteger initial_bci() const;
  inline void set_initial_bci(SmallInteger bci);
  inline void init_initial_bci(SmallInteger bci);

  inline SmallInteger num_args() const;
  inline void set_num_args(SmallInteger num);
  inline void init_num_args(SmallInteger num);

  inline Object copied(intptr_t index) const;
  inline void set_copied(intptr_t index, Object o);
  inline void init_copied(intptr_t index, Object o);

  inline Ref* from();
  inline Ref* to();
};

class Behavior : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Behavior, HeapObject);

 public:
  inline Behavior superclass() const;
  inline Array methods() const;
  inline AbstractMixin mixin() const;
  inline Object enclosing_object() const;
  inline SmallInteger id() const;
  inline void set_id(SmallInteger id);
  inline SmallInteger format() const;
};

class Class : public Behavior {
  HEAP_OBJECT_IMPLEMENTATION(Class, Behavior);

 public:
  inline String name() const;
  inline WeakArray subclasses() const;
};

class Metaclass : public Behavior {
  HEAP_OBJECT_IMPLEMENTATION(Metaclass, Behavior);

 public:
  inline Class this_class() const;
};

class AbstractMixin : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(AbstractMixin, HeapObject);

 public:
  inline String name() const;
  inline Array methods() const;
  inline AbstractMixin enclosing_mixin() const;
};

class Message : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Message, HeapObject);

 public:
  inline void set_selector(String selector);
  inline void init_selector(String selector);
  inline void set_arguments(Array arguments);
  inline void init_arguments(Array arguments);
};

class ObjectStore : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(ObjectStore, HeapObject);

 public:
  inline class SmallInteger size() const;
  inline Object nil_obj() const;
  inline Object false_obj() const;
  inline Object true_obj() const;
  inline Object message_loop() const;
  inline class Array common_selectors() const;
  inline class String does_not_understand() const;
  inline class String non_boolean_receiver() const;
  inline class String cannot_return() const;
  inline class String about_to_return_through() const;
  inline class String unused_bytecode() const;
  inline class String dispatch_message() const;
  inline class String dispatch_signal() const;
  inline Behavior Array() const;
  inline Behavior ByteArray() const;
  inline Behavior String() const;
  inline Behavior Closure() const;
  inline Behavior Ephemeron() const;
  inline Behavior Float64() const;
  inline Behavior LargeInteger() const;
  inline Behavior MediumInteger() const;
  inline Behavior Message() const;
  inline Behavior SmallInteger() const;
  inline Behavior WeakArray() const;
  inline Behavior Activation() const;
  inline Behavior Method() const;
};

class HeapObject::Layout {
 public:
  uword header_;
  uword header_hash_;
  Link incoming_;
};

class ForwardingCorpse::Layout : public HeapObject::Layout {
 public:
  intptr_t overflow_size_;
};

class FreeListElement::Layout : public HeapObject::Layout {
 public:
  intptr_t overflow_size_;
};

class MediumInteger::Layout : public HeapObject::Layout {
 public:
  int64_t value_;
};

class LargeInteger::Layout : public HeapObject::Layout {
 public:
  intptr_t capacity_;
  intptr_t negative_;  // TODO(rmacnak): Use a header bit?
  intptr_t size_;
  digit_t digits_[];
};

class RegularObject::Layout : public HeapObject::Layout {
 public:
  Ref/*Behavior*/ klass_;
  Ref/*Object*/ slots_[];
};

class Array::Layout : public HeapObject::Layout {
 public:
  SmallInteger size_;
  Ref/*Object*/ elements_[];
};

class WeakArray::Layout : public HeapObject::Layout {
 public:
  SmallInteger size_;  // Not visited.
  //!!!! WeakArray next_;  // Not visited.
  Ref/*Object*/ elements_[];
};

class Ephemeron::Layout : public HeapObject::Layout {
 public:
  Ref/*Behavior*/ klass_;
  Ref/*Object*/ key_;
  Ref/*Object*/ value_;
  Ref/*Object*/ finalizer_;
  //!!!!  Ephemeron next_;  // Not visited.
};

class Bytes::Layout : public HeapObject::Layout {
 public:
  SmallInteger size_;
};

class String::Layout : public Bytes::Layout {};

class ByteArray::Layout : public Bytes::Layout {};

class Method::Layout : public HeapObject::Layout {
 public:
  Ref/*Behavior*/ klass_;
  Ref/*SmallInteger*/ header_;
  Ref/*Array*/ literals_;
  Ref/*ByteArray*/ bytecode_;
  Ref/*AbstractMixin*/ mixin_;
  Ref/*String*/ selector_;
  Ref/*Object*/ source_;
};

class Activation::Layout : public HeapObject::Layout {
 public:
  Ref/*Activation*/ sender_;
  Ref/*SmallInteger*/ bci_;
  Ref/*Method*/ method_;
  Ref/*Closure*/ closure_;
  Ref/*Object*/ receiver_;
  Ref/*SmallInteger*/ stack_depth_;
  Ref/*Object*/ temps_[kMaxTemps];
};

class Float64::Layout : public HeapObject::Layout {
 public:
  double value_;
};

class Closure::Layout : public HeapObject::Layout {
 public:
  SmallInteger num_copied_;
  Ref/*Activation*/ defining_activation_;
  Ref/*SmallInteger*/ initial_bci_;
  Ref/*SmallInteger*/ num_args_;
  Ref/*Object*/ copied_[];
};

class Behavior::Layout : public HeapObject::Layout {
 public:
  Ref/*Behavior*/ klass_;
  Ref/*Behavior*/ superclass_;
  Ref/*Array*/ methods_;
  Ref/*Object*/ enclosing_object_;
  Ref/*AbstractMixin*/ mixin_;
  Ref/*SmallInteger*/ classid_;
  Ref/*SmallInteger*/ format_;
};

class Class::Layout : public Behavior::Layout {
 public:
  Ref/*String*/ name_;
  Ref/*WeakArray*/ subclasses_;
};

class Metaclass::Layout : public Behavior::Layout {
 public:
  Ref/*Class*/ this_class_;
};

class AbstractMixin::Layout : public HeapObject::Layout {
 public:
  Ref/*Behavior*/ klass_;
  Ref/*String*/ name_;
  Ref/*Array*/ methods_;
  Ref/*AbstractMixin*/ enclosing_mixin_;
};

class Message::Layout : public HeapObject::Layout {
 public:
  Ref/*Behavior*/ klass_;
  Ref/*String*/ selector_;
  Ref/*Array*/ arguments_;
};

class ObjectStore::Layout : public HeapObject::Layout {
 public:
  class SmallInteger array_size_;
  Ref/*Object*/ nil_;
  Ref/*Object*/ false_;
  Ref/*Object*/ true_;
  Ref/*Object*/ message_loop_;
  Ref/*class Array*/ common_selectors_;
  Ref/*class String*/ does_not_understand_;
  Ref/*class String*/ non_boolean_receiver_;
  Ref/*class String*/ cannot_return_;
  Ref/*class String*/ about_to_return_through_;
  Ref/*class String*/ unused_bytecode_;
  Ref/*class String*/ dispatch_message_;
  Ref/*class String*/ dispatch_signal_;
  Ref/*Behavior*/ Array_;
  Ref/*Behavior*/ ByteArray_;
  Ref/*Behavior*/ String_;
  Ref/*Behavior*/ Closure_;
  Ref/*Behavior*/ Ephemeron_;
  Ref/*Behavior*/ Float64_;
  Ref/*Behavior*/ LargeInteger_;
  Ref/*Behavior*/ MediumInteger_;
  Ref/*Behavior*/ Message_;
  Ref/*Behavior*/ SmallInteger_;
  Ref/*Behavior*/ WeakArray_;
  Ref/*Behavior*/ Activation_;
  Ref/*Behavior*/ Method_;
};

bool HeapObject::is_marked() const {
  return MarkBit::decode(ptr()->header_);
}
void HeapObject::set_is_marked(bool value) {
  ptr()->header_ = MarkBit::update(value, ptr()->header_);
}
bool HeapObject::is_weak_referent() const {
  return WeakReferentBit::decode(ptr()->header_);
}
void HeapObject::set_is_weak_referent(bool value) {
  ptr()->header_ = WeakReferentBit::update(value, ptr()->header_);
}
bool HeapObject::in_class_table() const {
  return InClassTableBit::decode(ptr()->header_);
}
void HeapObject::set_in_class_table(bool value) {
  ptr()->header_ = InClassTableBit::update(value, ptr()->header_);
}
bool HeapObject::is_canonical() const {
  return CanonicalBit::decode(ptr()->header_);
}
void HeapObject::set_is_canonical(bool value) {
  ptr()->header_ = CanonicalBit::update(value, ptr()->header_);
}
intptr_t HeapObject::heap_size() const {
  return SizeField::decode(ptr()->header_) << kObjectAlignmentLog2;
}
intptr_t HeapObject::cid() const {
  return ClassIdField::decode(ptr()->header_);
}
void HeapObject::set_cid(intptr_t value) {
  ptr()->header_ = ClassIdField::update(value, ptr()->header_);
}
intptr_t HeapObject::header_hash() const {
  return HashField::decode(ptr()->header_hash_);
}
void HeapObject::set_header_hash(intptr_t value) {
  ptr()->header_hash_ = HashField::update(value, ptr()->header_hash_);
}
intptr_t HeapObject::table_index() const {
  return IndexField::decode(ptr()->header_hash_);
}
void HeapObject::set_table_index(intptr_t value) {
  ptr()->header_hash_ = IndexField::update(value, ptr()->header_hash_);
}
Link* HeapObject::incoming() {
  return &ptr()->incoming_;
}

HeapObject HeapObject::Initialize(uword addr,
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
  HeapObject obj = FromAddr(addr);
  obj.ptr()->header_ = header;
  obj.ptr()->header_hash_ = 0;
  obj.ptr()->incoming_.Init();
  ASSERT(obj.cid() == cid);
  ASSERT(!obj.is_marked());
  return obj;
}

Object ForwardingCorpse::target() const {
  return static_cast<Object>(ptr()->header_hash_);
}
void ForwardingCorpse::set_target(Object value) {
  ptr()->header_hash_ = static_cast<uword>(value);
}
intptr_t ForwardingCorpse::overflow_size() const {
  return ptr()->overflow_size_;
}
void ForwardingCorpse::set_overflow_size(intptr_t value) {
  ptr()->overflow_size_ = value;
}

FreeListElement FreeListElement::next() const {
  return static_cast<FreeListElement>(ptr()->header_hash_);
}
void FreeListElement::set_next(FreeListElement value) {
  ASSERT((value == nullptr) || value->IsHeapObject());  // Tagged.
  ptr()->header_hash_ = static_cast<uword>(value);
}
intptr_t FreeListElement::overflow_size() const {
  return ptr()->overflow_size_;
}
void FreeListElement::set_overflow_size(intptr_t value) {
  ptr()->overflow_size_ = value;
}

int64_t MediumInteger::value() const { return ptr()->value_; }
void MediumInteger::set_value(int64_t value) { ptr()->value_ = value; }

bool LargeInteger::negative() const {
  return ptr()->negative_;
}
void LargeInteger::set_negative(bool v) {
  ptr()->negative_ = v;
}
intptr_t LargeInteger::size() const {
  return ptr()->size_;
}
void LargeInteger::set_size(intptr_t value) {
  ptr()->size_ = value;
}
intptr_t LargeInteger::capacity() const {
  return ptr()->capacity_;
}
void LargeInteger::set_capacity(intptr_t value) {
  ptr()->capacity_ = value;
}
digit_t LargeInteger::digit(intptr_t index) const {
  return ptr()->digits_[index];
}
void LargeInteger::set_digit(intptr_t index, digit_t value) {
  ptr()->digits_[index] = value;
}

void RegularObject::init_klass(Behavior value) {
  Init<Behavior>(&ptr()->klass_, value);
}
void RegularObject::set_klass(Behavior value) {
  Store<Behavior>(&ptr()->klass_, value);
}
Object RegularObject::slot(intptr_t index) const {
  return Load<Object>(&ptr()->slots_[index]);
}
void RegularObject::set_slot(intptr_t index, Object value) {
  Store<Object>(&ptr()->slots_[index], value);
}
void RegularObject::init_slot(intptr_t index, Object value) {
  Init<Object>(&ptr()->slots_[index], value);
}
Ref* RegularObject::from() {
  return &ptr()->klass_;
}
Ref* RegularObject::to() {
  intptr_t num_slots =
      (heap_size() - sizeof(RegularObject::Layout)) / sizeof(Ref);
  return &ptr()->slots_[num_slots - 1];
}

SmallInteger Array::size() const {
  //return Load(&ptr()->size_, kNoBarrier);
  return ptr()->size_;
}
void Array::set_size(SmallInteger s) {
  //Store(&ptr()->size_, s, kNoBarrier);
  ptr()->size_ = s;
}
void Array::init_size(SmallInteger s) {
  //Store(&ptr()->size_, s, kNoBarrier);
  ptr()->size_ = s;
}
Object Array::element(intptr_t index) const {
  return Load<Object>(&ptr()->elements_[index]);
}
void Array::set_element(intptr_t index, Object value) {
  Store(&ptr()->elements_[index], value);
}
void Array::init_element(intptr_t index, Object value) {
  Init(&ptr()->elements_[index], value);
}
Ref* Array::from() {
  return &ptr()->elements_[0];
}
Ref* Array::to() {
  return &ptr()->elements_[Size() - 1];
}

SmallInteger WeakArray::size() const {
  return ptr()->size_;
}
void WeakArray::set_size(SmallInteger s) {
  ptr()->size_ = s;
}
void WeakArray::init_size(SmallInteger s) {
  ptr()->size_ = s;
}
Object WeakArray::element(intptr_t index) const {
  return Load<Object>(&ptr()->elements_[index]);
}
void WeakArray::set_element(intptr_t index, Object value) {
  Store<Object>(&ptr()->elements_[index], value);
}
void WeakArray::init_element(intptr_t index, Object value) {
  Init<Object>(&ptr()->elements_[index], value);
}
Ref* WeakArray::from() {
  return &ptr()->elements_[0];
}
Ref* WeakArray::to() {
  return &ptr()->elements_[Size() - 1];
}

Object Ephemeron::key() const { return Load<Object>(&ptr()->key_); }
void Ephemeron::set_key(Object key) {
  Store<Object>(&ptr()->key_, key);
}
void Ephemeron::init_key(Object key) {
  Init<Object>(&ptr()->key_, key);
}
Object Ephemeron::value() const { return Load<Object>(&ptr()->value_); }
void Ephemeron::set_value(Object value) {
  Store<Object>(&ptr()->value_, value);
}
void Ephemeron::init_value(Object value) {
  Init<Object>(&ptr()->value_, value);
}
Object Ephemeron::finalizer() const { return Load<Object>(&ptr()->finalizer_); }
void Ephemeron::set_finalizer(Object finalizer) {
  Store<Object>(&ptr()->finalizer_, finalizer);
}
void Ephemeron::init_finalizer(Object finalizer) {
  Init<Object>(&ptr()->finalizer_, finalizer);
}
Ref* Ephemeron::from() { return &ptr()->key_; }
Ref* Ephemeron::to() { return &ptr()->finalizer_; }

SmallInteger Bytes::size() const {
  return ptr()->size_;
}

void Bytes::set_size(SmallInteger s) {
  ptr()->size_ = s;
}
void Bytes::init_size(SmallInteger s) {
  ptr()->size_ = s;
}
uint8_t Bytes::element(intptr_t index) const {
  return *element_addr(index);
}
void Bytes::set_element(intptr_t index, uint8_t value) {
  *element_addr(index) = value;
}
uint8_t* Bytes::element_addr(intptr_t index) {
  uint8_t* elements = reinterpret_cast<uint8_t*>(
      reinterpret_cast<uword>(ptr()) + sizeof(Bytes::Layout));
  return &elements[index];
}
const uint8_t* Bytes::element_addr(intptr_t index) const {
  const uint8_t* elements = reinterpret_cast<const uint8_t*>(
      reinterpret_cast<uword>(ptr()) + sizeof(Bytes::Layout));
  return &elements[index];
}

SmallInteger Method::header() const { return Load<SmallInteger>(&ptr()->header_); }
Array Method::literals() const { return Load<Array>(&ptr()->literals_); }
ByteArray Method::bytecode() const { return Load<ByteArray>(&ptr()->bytecode_); }
AbstractMixin Method::mixin() const { return Load<AbstractMixin>(&ptr()->mixin_); }
String Method::selector() const { return Load<String>(&ptr()->selector_); }
Object Method::source() const { return Load<Object>(&ptr()->source_); }

Activation Activation::sender() const { return Load<Activation>(&ptr()->sender_); }
void Activation::set_sender(Activation s) {
  Store<Activation>(&ptr()->sender_, s);
}
void Activation::init_sender(Activation s) {
  Init<Activation>(&ptr()->sender_, s);
}
SmallInteger Activation::bci() const {
  return Load<SmallInteger>(&ptr()->bci_);
}
void Activation::set_bci(SmallInteger i) {
  Store<SmallInteger>(&ptr()->bci_, i);
}
void Activation::init_bci(SmallInteger i) {
  Init<SmallInteger>(&ptr()->bci_, i);
}
Method Activation::method() const {
  return Load<Method>(&ptr()->method_);
}
void Activation::set_method(Method m) {
  Store<Method>(&ptr()->method_, m);
}
void Activation::init_method(Method m) {
  Init<Method>(&ptr()->method_, m);
}
Closure Activation::closure() const {
  return Load<Closure>(&ptr()->closure_);
}
void Activation::set_closure(Closure m) {
  Store<Closure>(&ptr()->closure_, m);
}
void Activation::init_closure(Closure m) {
  Init<Closure>(&ptr()->closure_, m);
}
Object Activation::receiver() const {
  return Load<Object>(&ptr()->receiver_);
}
void Activation::set_receiver(Object o) {
  Store<Object>(&ptr()->receiver_, o);
}
void Activation::init_receiver(Object o) {
  Init<Object>(&ptr()->receiver_, o);
}
SmallInteger Activation::stack_depth() const {
  return Load<SmallInteger>(&ptr()->stack_depth_);
}
void Activation::set_stack_depth(SmallInteger d) {
  Store<SmallInteger>(&ptr()->stack_depth_, d);
}
void Activation::init_stack_depth(SmallInteger d) {
  Init<SmallInteger>(&ptr()->stack_depth_, d);
}
Object Activation::temp(intptr_t index) const {
  return Load<Object>(&ptr()->temps_[index]);
}
void Activation::set_temp(intptr_t index, Object o) {
  Store<Object>(&ptr()->temps_[index], o);
}
void Activation::init_temp(intptr_t index, Object o) {
  Init<Object>(&ptr()->temps_[index], o);
}
Ref* Activation::from() {
  return &ptr()->sender_;
}
Ref* Activation::to() {
  //  return &ptr()->stack_depth_ + StackDepth();
  return &ptr()->temps_[kMaxTemps - 1];
}

double Float64::value() const { return ptr()->value_; }
void Float64::set_value(double v) { ptr()->value_ = v; }

SmallInteger Closure::num_copied() const {
  //return Load(&ptr()->num_copied_, kNoBarrier);
  return ptr()->num_copied_;
}
void Closure::set_num_copied(SmallInteger v) {
  //Store(&ptr()->num_copied_, v, kNoBarrier);
  ptr()->num_copied_ = v;
}
void Closure::init_num_copied(SmallInteger v) {
  //Store(&ptr()->num_copied_, v, kNoBarrier);
  ptr()->num_copied_ = v;
}
Activation Closure::defining_activation() const {
  return Load<Activation>(&ptr()->defining_activation_);
}
void Closure::set_defining_activation(Activation a) {
  Store<Activation>(&ptr()->defining_activation_, a);
}
void Closure::init_defining_activation(Activation a) {
  Init<Activation>(&ptr()->defining_activation_, a);
}
SmallInteger Closure::initial_bci() const {
  return Load<SmallInteger>(&ptr()->initial_bci_);
}
void Closure::set_initial_bci(SmallInteger bci) {
  Store<SmallInteger>(&ptr()->initial_bci_, bci);
}
void Closure::init_initial_bci(SmallInteger bci) {
  Init<SmallInteger>(&ptr()->initial_bci_, bci);
}
SmallInteger Closure::num_args() const {
  return Load<SmallInteger>(&ptr()->num_args_);
}
void Closure::set_num_args(SmallInteger num) {
  Store<SmallInteger>(&ptr()->num_args_, num);
}
void Closure::init_num_args(SmallInteger num) {
  Init<SmallInteger>(&ptr()->num_args_, num);
}
Object Closure::copied(intptr_t index) const {
  return Load<Object>(&ptr()->copied_[index]);
}
void Closure::set_copied(intptr_t index, Object o) {
  Store<Object>(&ptr()->copied_[index], o);
}
void Closure::init_copied(intptr_t index, Object o) {
  Init<Object>(&ptr()->copied_[index], o);
}
Ref* Closure::from() {
  //return (&ptr()->num_copied_);
  return (&ptr()->defining_activation_);
}
Ref* Closure::to() {
  return (&ptr()->copied_[NumCopied() - 1]);
}

Behavior Behavior::superclass() const {
  return Load<Behavior>(&ptr()->superclass_);
}
Array Behavior::methods() const {
  return Load<Array>(&ptr()->methods_);
}
AbstractMixin Behavior::mixin() const {
  return Load<AbstractMixin>(&ptr()->mixin_);
}
Object Behavior::enclosing_object() const {
  return Load<Object>(&ptr()->enclosing_object_);
}
SmallInteger Behavior::id() const {
  return Load<SmallInteger>(&ptr()->classid_);
}
void Behavior::set_id(SmallInteger id) {
  Store<SmallInteger>(&ptr()->classid_, id);
}
SmallInteger Behavior::format() const {
  return Load<SmallInteger>(&ptr()->format_);
}

String Class::name() const {
  return Load<String>(&ptr()->name_);
}
WeakArray Class::subclasses() const {
  return Load<WeakArray>(&ptr()->subclasses_);
}

Class Metaclass::this_class() const {
  return Load<Class>(&ptr()->this_class_);
}

String AbstractMixin::name() const { return Load<String>(&ptr()->name_); }
Array AbstractMixin::methods() const { return Load<Array>(&ptr()->methods_); }
AbstractMixin AbstractMixin::enclosing_mixin() const {
  return Load<AbstractMixin>(&ptr()->enclosing_mixin_);
}

void Message::set_selector(String selector) {
  Store<String>(&ptr()->selector_, selector);
}
void Message::init_selector(String selector) {
  Init<String>(&ptr()->selector_, selector);
}
void Message::set_arguments(Array arguments) {
  Store<Array>(&ptr()->arguments_, arguments);
}
void Message::init_arguments(Array arguments) {
  Init<Array>(&ptr()->arguments_, arguments);
}

class SmallInteger ObjectStore::size() const {
  //return Load<SmallInteger>(&ptr()->array_size_);
  return ptr()->array_size_;
}
Object ObjectStore::nil_obj() const {
  return Load<Object>(&ptr()->nil_);
}
Object ObjectStore::false_obj() const {
  return Load<Object>(&ptr()->false_);
}
Object ObjectStore::true_obj() const {
  return Load<Object>(&ptr()->true_);
}
Object ObjectStore::message_loop() const {
  return Load<Object>(&ptr()->message_loop_);
}
class Array ObjectStore::common_selectors() const {
  return Load<class Array>(&ptr()->common_selectors_);
}
class String ObjectStore::does_not_understand() const {
  return Load<class String>(&ptr()->does_not_understand_);
}
class String ObjectStore::non_boolean_receiver() const {
  return Load<class String>(&ptr()->non_boolean_receiver_);
}
class String ObjectStore::cannot_return() const {
  return Load<class String>(&ptr()->cannot_return_);
}
class String ObjectStore::about_to_return_through() const {
  return Load<class String>(&ptr()->about_to_return_through_);
}
class String ObjectStore::unused_bytecode() const {
  return Load<class String>(&ptr()->unused_bytecode_);
}
class String ObjectStore::dispatch_message() const {
  return Load<class String>(&ptr()->dispatch_message_);
}
class String ObjectStore::dispatch_signal() const {
  return Load<class String>(&ptr()->dispatch_signal_);
}
Behavior ObjectStore::Array() const { return Load<Behavior>(&ptr()->Array_); }
Behavior ObjectStore::ByteArray() const { return Load<Behavior>(&ptr()->ByteArray_); }
Behavior ObjectStore::String() const { return Load<Behavior>(&ptr()->String_); }
Behavior ObjectStore::Closure() const { return Load<Behavior>(&ptr()->Closure_); }
Behavior ObjectStore::Ephemeron() const { return Load<Behavior>(&ptr()->Ephemeron_); }
Behavior ObjectStore::Float64() const { return Load<Behavior>(&ptr()->Float64_); }
Behavior ObjectStore::LargeInteger() const { return Load<Behavior>(&ptr()->LargeInteger_); }
Behavior ObjectStore::MediumInteger() const { return Load<Behavior>(&ptr()->MediumInteger_); }
Behavior ObjectStore::Message() const { return Load<Behavior>(&ptr()->Message_); }
Behavior ObjectStore::SmallInteger() const { return Load<Behavior>(&ptr()->SmallInteger_); }
Behavior ObjectStore::WeakArray() const { return Load<Behavior>(&ptr()->WeakArray_); }
Behavior ObjectStore::Activation() const { return Load<Behavior>(&ptr()->Activation_); }
Behavior ObjectStore::Method() const { return Load<Behavior>(&ptr()->Method_); }


void Ref::Init(Object source, Object target) {
#if defined(DEBUG)
  Link::Init();
#endif
  from = source;
  to = target;
  if (target->IsHeapObject()) {
    static_cast<HeapObject>(target)->incoming()->Insert(this);
    ASSERT(next != this);
    ASSERT(prev != this);
    ASSERT(next != nullptr);
    ASSERT(prev != nullptr);
  } else {
    ASSERT(next == this);
    ASSERT(prev == this);
  }
}

void Ref::Update(Object source, Object new_target) {
  ASSERT(from == source);
  ASSERT(source == nullptr || source->IsHeapObject());
  UpdateNoCheck(new_target);
}

void Ref::UpdateNoCheck(Object new_target) {
  if (to->IsHeapObject()) {
    Remove();
  } else {
    ASSERT(next == this);
    ASSERT(prev == this);
  }
  to = new_target;
  if (new_target->IsHeapObject()) {
    static_cast<HeapObject>(new_target)->incoming()->Insert(this);
    ASSERT(next != this);
    ASSERT(prev != this);
    ASSERT(next != nullptr);
    ASSERT(prev != nullptr);
  } else {
    ASSERT(next == this);
    ASSERT(prev == this);
  }
}

}  // namespace psoup

#endif  // VM_OBJECT_H_
