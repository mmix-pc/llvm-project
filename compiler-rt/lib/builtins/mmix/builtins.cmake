# clzti2 and udivmodti4 close the wide conversion and division dependencies.
set(mmix_SOURCES
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

set(mmix_ATOMIC_SOURCES
  mmix/atomic.c)

set(mmix_STACK_PROTECTOR_SOURCES
  mmix/stack_protector_fail.c
  mmix/stack_protector_guard.c)

function(add_mmix_crt_objects)
  # Generic-system CMake defaults to .obj; this is an ELF CRT object.
  set(CMAKE_C_OUTPUT_EXTENSION .o)
  # Static linker bounds own frame discovery; no runtime registry is needed.
  foreach(name crtbegin crtend)
    add_compiler_rt_runtime(clang_rt.${name}
      OBJECT
      ARCHS mmix
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c
      CFLAGS ${BUILTIN_CFLAGS_mmix} -DCRT_HAS_INITFINI_ARRAY -UEH_USE_FRAME_REGISTRY
      PARENT_TARGET mmix-crt)
  endforeach()
endfunction()
