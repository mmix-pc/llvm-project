#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Threading.h"

static_assert(LLVM_ENABLE_THREADS == 0);
static llvm::ManagedStatic<llvm::SmallVector<int, 4>> Values;

extern "C" void llvm_state_consumer(llvm::sys::Mutex &mutex,
                                    llvm::sys::RWMutex &rw,
                                    llvm::once_flag &once) {
  mutex.lock();
  Values->push_back(1);
  mutex.unlock();
  rw.lock_shared();
  rw.unlock_shared();
  rw.lock();
  rw.unlock();
  llvm::call_once(once, [] {});
  llvm::llvm_shutdown();
}
