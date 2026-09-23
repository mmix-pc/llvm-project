option(LIBC_MMIX_BUILD_MATH "Build Linux LLVM Support math and fenv providers" OFF)
if(NOT LIBC_MMIX_BUILD_MATH)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.math.acos
  libc.src.math.asin
  libc.src.math.atan
  libc.src.math.atan2
  libc.src.math.cos
  libc.src.math.cosh
  libc.src.math.erf
  libc.src.math.exp
  libc.src.math.log
  libc.src.math.log1p
  libc.src.math.log2
  libc.src.math.logb
  libc.src.math.pow
  libc.src.math.sin
  libc.src.math.sinh
  libc.src.math.tan
  libc.src.math.tanh
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
