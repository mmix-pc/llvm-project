// Compile/link input for the static Linux signal runtime; execution needs Linux.
// Keep all functions in the linked object, including the failure paths.
#ifdef REVERSE_INCLUDES
#include <ucontext.h>
#include <sys/wait.h>
#include <spawn.h>
#include <setjmp.h>
#include <signal.h>
#endif
#include <signal.h>
#include <setjmp.h>
#include <spawn.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static jmp_buf plain;
static sigjmp_buf masked;
static volatile sig_atomic_t received;
static void recover(int number) { received = number; longjmp(plain, 1); }
static void recover_mask(int number, siginfo_t *info, void *context) {
  ucontext_t *uc = (ucontext_t *)context;
  if (info && uc && info->si_signo == number)
    received = (sig_atomic_t)uc->uc_mcontext.sc_regs.pc;
  siglongjmp(masked, 1);
}

int register_and_recover(void) {
  struct sigaction action = {0}, old;
  stack_t stack = {0}, previous;
  sigset_t blocked, saved;
  stack.ss_size = MINSIGSTKSZ + 65536;
  stack.ss_sp = malloc(stack.ss_size);
  if (!stack.ss_sp)
    return -1;
  if (sigaltstack(&stack, &previous)) {
    free(stack.ss_sp);
    return -1;
  }
  sigemptyset(&action.sa_mask);
  sigfillset(&blocked);
  sigdelset(&blocked, SIGUSR1);
  sigaddset(&blocked, SIGUSR2);
  action.sa_sigaction = recover_mask;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND;
  if (sigaction(SIGUSR1, &action, &old))
    abort();
  if (sigprocmask(SIG_BLOCK, &blocked, &saved))
    abort();
  if (!sigsetjmp(masked, 1))
    raise(SIGUSR1);
  if (sigprocmask(SIG_SETMASK, &saved, 0) || sigaction(SIGUSR1, &old, 0))
    abort();
  if (sigaltstack(&previous, 0))
    abort();
  free(stack.ss_sp);
  void (*previous_handler)(int) = signal(SIGUSR1, recover);
  if (previous_handler == SIG_ERR)
    return -1;
  if (!setjmp(plain))
    raise(SIGUSR1);
  if (signal(SIGUSR1, previous_handler) == SIG_ERR)
    return -1;
  return received;
}

int spawn_and_wait(const char *path, char *const args[], char *const env[],
                   const char *output) {
  posix_spawn_file_actions_t actions;
  pid_t pid;
  int status;
  int error = posix_spawn_file_actions_init(&actions);
  if (error)
    return error;
  error = posix_spawn_file_actions_addopen(&actions, 1, output,
                                          O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (!error)
    error = posix_spawn_file_actions_adddup2(&actions, 1, 2);
  if (!error)
    error = posix_spawn(&pid, path, &actions, 0, args, env);
  posix_spawn_file_actions_destroy(&actions);
  if (error)
    return error;
  if (waitpid(pid, &status, 0) != pid)
    return errno;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int fork_exec_wait(const char *path, char *const args[], char *const env[]) {
  assert(path && args && env);
  pid_t pid = fork();
  if (!pid) {
    execve(path, args, env);
    _exit(127);
  }
  if (pid < 0)
    return errno;
  int status;
  pid_t waited = waitpid(pid, &status, WNOHANG);
  if (waited < 0)
    return errno;
  if (waited == 0) {
    if (kill(pid, SIGTERM))
      return errno;
    if (waitpid(pid, &status, 0) != pid)
      return errno;
  }
  return status;
}

// The CRT uses C linkage, including freestanding C++ consumers.
#ifdef __cplusplus
extern "C"
#endif
int main(void) { return 0; }
