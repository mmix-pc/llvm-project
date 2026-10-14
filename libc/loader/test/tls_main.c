// Link-only input for host inspection of MMIX dynamic TLS relocation.
__thread long main_gd = 11;
__thread long main_ie = 12;
// Not a resolver implementation; this image must not be executed.
void *__tls_get_addr(void *p) { return p; }
#ifndef PROVIDER
extern long access_tls(void);
long _start(void) { return access_tls(); }
#endif
