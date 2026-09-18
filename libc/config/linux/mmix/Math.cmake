option(LIBC_MMIX_BUILD_MATH "Build Linux LLVM Support math and fenv providers" OFF)
if(NOT LIBC_MMIX_BUILD_MATH)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.math.sqrt
  libc.src.math.log10
  libc.src.fenv.feclearexcept
  libc.src.fenv.fetestexcept
  libc.src.fenv.fegetexceptflag
  libc.src.fenv.fesetexceptflag
  libc.src.fenv.feraiseexcept
  libc.src.fenv.fegetround
  libc.src.fenv.fesetround
  libc.src.fenv.fegetenv
  libc.src.fenv.fesetenv
  libc.src.fenv.feholdexcept
  libc.src.fenv.feupdateenv
)

include(${CMAKE_CURRENT_LIST_DIR}/MathLinkerScript.cmake)
