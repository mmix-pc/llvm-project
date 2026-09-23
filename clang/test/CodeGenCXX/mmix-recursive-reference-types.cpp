// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -O0 -emit-obj -o %t.o %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -O2 -emit-obj -o %t.o %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++17 -DNEGATIVE=1 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VECTOR
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++17 -DNEGATIVE=2 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=AS
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++17 -DNEGATIVE=3 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VECTOR
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -std=c++17 -O0 -emit-obj -o %t.o %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -std=c++17 -O2 -emit-obj -o %t.o %s
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -DNEGATIVE=1 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VECTOR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -DNEGATIVE=2 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=AS
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -DNEGATIVE=3 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VECTOR

#if !NEGATIVE
struct Node {
  Node &next;
  int value;
};
using Alias = Node;
extern "C" int self(Alias &node) {
  Alias &ref = node;
  Alias copy = ref;
  return copy.next.value;
}

struct Right;
struct Left { Right &right; };
struct Right { Left &left; int value; };
extern "C" int mutual(Left &left) { return left.right.value; }

struct Rvalue { Rvalue &&next; int value; };
extern "C" int rvalue(Rvalue &node) { return node.next.value; }

struct Qualified { const Qualified &next; int value; };
extern "C" int qualified(const Qualified &node) { return node.next.value; }

struct Array { Node elements[2]; };
extern "C" int array(Array &nodes) { return nodes.elements[1].next.value; }

struct Holder {
  Node &node;
  Holder(Node &n) : node(n) {}
  int read() const { return node.value; }
};
extern "C" int construct(Node &node) {
  Holder holder(node);
  return holder.read();
}
#else
typedef int wide __attribute__((ext_vector_type(16)));
typedef int small __attribute__((ext_vector_type(2)));
struct VectorRecord { small value; };
struct Cycle {
  Cycle &next;
#if NEGATIVE == 1
  wide unsupported;
#elif NEGATIVE == 2
  int __attribute__((address_space(1))) *unsupported;
#elif NEGATIVE == 3
  VectorRecord ordinary;
  _Atomic(VectorRecord) atomic;
#endif
};
extern "C" void check(Cycle &value) {
  Cycle &alias = value;
  (void)alias;
}
// VECTOR: error: MMIX GNU ABI does not support vector value CodeGen
// AS: error: MMIX GNU ABI does not support nonzero-address-space value CodeGen
#endif
