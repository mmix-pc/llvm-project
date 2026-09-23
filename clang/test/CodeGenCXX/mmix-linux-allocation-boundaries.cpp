// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++17 -DCASE=0 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VECTOR
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++17 -DCASE=1 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VARIADIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++20 -DCASE=2 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=DESTROYING
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -DCASE=3 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++26 -DCASE=4 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=TYPE-NEW
// RUN: not %clang_cc1 -triple mmix-unknown-linux -std=c++26 -DCASE=5 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=TYPE-DELETE

using size_t = __SIZE_TYPE__;
#if CASE == 0
typedef int wide __attribute__((ext_vector_type(16)));
struct Arg { wide value; };
struct Object { static void *operator new(size_t, Arg); };
Object *unsupported(Arg &arg) { return new (arg) Object; }
// VECTOR: error: MMIX GNU ABI does not support vector value CodeGen
// VECTOR-NOT: MMIX does not support C++ allocation form
#elif CASE == 1
struct Object { static void *operator new(size_t, ...); };
Object *unsupported() { return new (1) Object; }
// VARIADIC: error: MMIX does not support C++ allocation form
#elif CASE == 2
namespace std { struct destroying_delete_t {}; }
struct Object {
  static void operator delete(Object *, std::destroying_delete_t);
};
void unsupported(Object *p) { delete p; }
// DESTROYING: error: MMIX does not support C++ deallocation form
#elif CASE == 3
struct Object { static void *operator new(size_t); };
Object *unsupported() { return new Object; }
// GENERIC: error: MMIX does not support C++ allocation form
#else
namespace std {
template <class T> struct type_identity { using type = T; };
enum class align_val_t : size_t {};
}
struct Object {};
template <class T>
void *operator new(std::type_identity<T>, size_t, std::align_val_t);
template <class T>
void operator delete(std::type_identity<T>, void *, size_t, std::align_val_t) noexcept;
#if CASE == 4
Object *unsupported() { return new Object; }
// TYPE-NEW: error: MMIX does not support C++ allocation form
#else
void unsupported(Object *p) { delete p; }
// TYPE-DELETE: error: MMIX does not support C++ deallocation form
#endif
#endif
