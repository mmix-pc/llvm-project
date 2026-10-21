# clzti2 and udivmodti4 close the wide conversion and division dependencies.
# Reuse the ordinary C atomic providers with Clang's stdatomic.h flag ABI.
set(mmix_SOURCES
  atomic_flag_clear.c
  atomic_flag_clear_explicit.c
  atomic_flag_test_and_set.c
  atomic_flag_test_and_set_explicit.c
  atomic_signal_fence.c
  atomic_thread_fence.c
  clzti2.c
  divdc3.c
  divsc3.c
  divti3.c
  ffsdi2.c
  fixdfti.c
  fixsfti.c
  fixunsdfti.c
  fixunssfti.c
  floattidf.c
  floattisf.c
  floatuntidf.c
  floatuntisf.c
  modti3.c
  muldc3.c
  muloti4.c
  mulsc3.c
  multi3.c
  udivmodti4.c
  udivti3.c
  umodti3.c)

# The Linux C cleanup personality shares the DWARF unwinder with C++.
# The build system may be Generic even when the compiler targets Linux.
if(CMAKE_C_COMPILER_TARGET MATCHES "^mmix-unknown-linux(-unknown)?$")
  if(NOT HAVE_UNWIND_H)
    message(FATAL_ERROR "MMIX Linux builtins require the compiler unwind.h")
  endif()
  list(APPEND mmix_SOURCES gcc_personality_v0.c int_util.c)
endif()

set(mmix_ATOMIC_SOURCES
  mmix/atomic.c)

set(mmix_STACK_PROTECTOR_SOURCES
  mmix/stack_protector_fail.c
  mmix/stack_protector_guard.c)

function(add_mmix_crt_objects)
  # Generic-system CMake defaults to .obj; this is an ELF CRT object.
  set(CMAKE_C_OUTPUT_EXTENSION .o)
  # Static bounds or the resident loader own frame discovery, not the CRT.
  set(crt_flags ${BUILTIN_CFLAGS_mmix} -DCRT_HAS_INITFINI_ARRAY
    -UEH_USE_FRAME_REGISTRY)
  if(CMAKE_C_COMPILER_TARGET MATCHES "^mmix-unknown-linux(-unknown)?$")
    # One PIC pair serves static executables, PIE and DSOs. Keep each image's
    # hidden self-address handle without a separate shared-runtime CRT owner.
    list(APPEND crt_flags -fPIC)
  endif()
  foreach(name crtbegin crtend)
    add_compiler_rt_runtime(clang_rt.${name}
      OBJECT
      ARCHS mmix
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c
      CFLAGS ${crt_flags}
      PARENT_TARGET mmix-crt)
  endforeach()
endfunction()
