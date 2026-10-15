// Link-only input for native inspection; target code is never executed.
long object = 7;
#ifdef INITIAL_EXEC
#define MODEL __attribute__((tls_model("initial-exec")))
#else
#define MODEL __attribute__((tls_model("global-dynamic")))
#endif
__thread long *tls_pointer MODEL = &object;
__thread long tls_zero MODEL;
__thread long local_tls
    __attribute__((tls_model("local-dynamic"), visibility("hidden"))) = 11;
long access_tls(void) { return *tls_pointer + tls_zero + local_tls; }
// Satisfy the fixture's import without pretending to implement target TLS.
void *__tls_get_addr(void *descriptor) { return descriptor; }
#ifdef MISSING
extern long absent;
long *missing = &absent;
#endif
