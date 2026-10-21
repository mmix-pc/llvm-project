// Exercise ordinary function entities separately from the stdatomic macros.
#include <stdatomic.h>
#include <stdbool.h>

static void (*volatile thread_fence)(memory_order) = &atomic_thread_fence;
static void (*volatile signal_fence)(memory_order) = &atomic_signal_fence;
static void (*volatile clear)(volatile atomic_flag *) = &atomic_flag_clear;
static void (*volatile clear_explicit)(volatile atomic_flag *, memory_order) =
    &atomic_flag_clear_explicit;
static bool (*volatile test_set)(volatile atomic_flag *) =
    &atomic_flag_test_and_set;
static bool (*volatile test_set_explicit)(volatile atomic_flag *, memory_order) =
    &atomic_flag_test_and_set_explicit;

int main(void) {
  // Adjacent flags detect an incorrectly widened store or endian lane access.
  volatile atomic_flag flags[3] = {ATOMIC_FLAG_INIT, ATOMIC_FLAG_INIT,
                                   ATOMIC_FLAG_INIT};
  const memory_order orders[] = {
      memory_order_relaxed, memory_order_consume, memory_order_acquire,
      memory_order_release, memory_order_acq_rel, memory_order_seq_cst};
  const memory_order store_orders[] = {
      memory_order_relaxed, memory_order_release, memory_order_seq_cst};
  atomic_flag_test_and_set(&flags[0]);
  atomic_flag_test_and_set(&flags[2]);
  if (test_set(&flags[1]) || !atomic_flag_test_and_set(&flags[1]))
    return 1;
  clear(&flags[1]);
  if (atomic_flag_test_and_set(&flags[1]) || !test_set(&flags[1]))
    return 2;
  for (unsigned i = 0; i < sizeof(orders) / sizeof(orders[0]); ++i) {
    atomic_flag_clear(&flags[1]);
    if (test_set_explicit(&flags[1], orders[i]) ||
        !atomic_flag_test_and_set_explicit(&flags[1], orders[i]))
      return 3;
    thread_fence(orders[i]);
    signal_fence(orders[i]);
  }
  for (unsigned i = 0; i < sizeof(store_orders) / sizeof(store_orders[0]); ++i) {
    clear_explicit(&flags[1], store_orders[i]);
    if (atomic_flag_test_and_set(&flags[1]))
      return 4;
  }
  // Parentheses suppress function-like macros, including direct calls.
  (atomic_flag_clear)(&flags[1]);
  if ((atomic_flag_test_and_set)(&flags[1]))
    return 5;
  (atomic_flag_clear_explicit)(&flags[1], memory_order_release);
  if ((atomic_flag_test_and_set_explicit)(&flags[1], memory_order_acquire))
    return 6;
  (atomic_thread_fence)(memory_order_seq_cst);
  (atomic_signal_fence)(memory_order_seq_cst);
  if (!test_set(&flags[0]) || !test_set(&flags[2]))
    return 7;
  return 0;
}
