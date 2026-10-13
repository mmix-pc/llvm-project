# ELF metadata fixture, not an executable application.
.text
.globl alpha, beta, gamma, hook
.type alpha,@function
.type beta,@function
.protected beta
.weak weak_hook, optional
.type weak_hook,@function
.type gamma,@function
.hidden hook
.p2align 2
alpha: POP 0,0
beta: POP 0,0
gamma: POP 0,0
hook: POP 0,0
weak_hook: POP 0,0
.ifdef RESOLVE_MISSING
.globl missing
.type missing,@function
missing: POP 0,0
.endif
.section .rodata,"a",@progbits
.p2align 3
.mmix_got_pc64 alpha
.data
.p2align 3
.globl pointer
.type pointer,@object
.size pointer,8
pointer: .quad alpha
.quad missing
.quad optional
.quad alpha+8
.quad alpha-8
.quad hook
.section .init_array,"aw",@init_array
.p2align 3
.quad hook
.section .fini_array,"aw",@fini_array
.p2align 3
.quad hook
.section .note.GNU-stack,"",@progbits
