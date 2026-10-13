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
.data
.p2align 3
.globl pointer
.type pointer,@object
.size pointer,8
pointer: .quad alpha
.quad missing
.quad optional
.section .init_array,"aw",@init_array
.p2align 3
.quad hook
.section .fini_array,"aw",@fini_array
.p2align 3
.quad hook
.section .note.GNU-stack,"",@progbits
