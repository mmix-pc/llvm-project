// Link-only input for host inspection of MMIX dynamic TLS relocation.
extern __thread long main_gd;
extern __thread long main_ie __attribute__((tls_model("initial-exec")));
__thread volatile long own_ld
    __attribute__((visibility("protected"), tls_model("local-dynamic"))) = 7;
__thread volatile long own_ie
    __attribute__((visibility("protected"), tls_model("initial-exec"))) = 8;
long access_tls(void) { return main_gd + main_ie + own_ld + own_ie; }
