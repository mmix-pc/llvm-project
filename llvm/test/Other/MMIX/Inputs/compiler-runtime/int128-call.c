typedef __int128 I;
typedef unsigned __int128 U;

I divide(I a, I b) { return a / b; }
U unsigned_divide(U a, U b) { return a / b; }
I remainder(I a, I b) { return a % b; }
U unsigned_remainder(U a, U b) { return a % b; }
int overflow(I a, I b, I *result) {
  return __builtin_mul_overflow(a, b, result);
}
float to_float(I a) { return a; }
double to_double(I a) { return a; }
float unsigned_to_float(U a) { return a; }
double unsigned_to_double(U a) { return a; }
I from_float(float a) { return a; }
I from_double(double a) { return a; }
U unsigned_from_float(float a) { return a; }
U unsigned_from_double(double a) { return a; }

// Ordinary multiplication lowers natively; retain an explicit fallback caller.
extern I __multi3(I, I);
I multiply(I a, I b) { return __multi3(a, b); }

// This fixture qualifies symbol closure, not process startup or execution.
void _start(void) {}
