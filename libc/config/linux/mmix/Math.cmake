option(LIBC_MMIX_BUILD_MATH "Build the C17 math, complex and fenv providers" OFF)
if(NOT LIBC_MMIX_BUILD_MATH)
  return()
endif()

foreach(name
    acos asin atan atan2 cos sin tan acosh asinh atanh cosh sinh tanh
    exp exp2 expm1 frexp ilogb ldexp log log10 log1p log2 logb modf scalbn scalbln
    cbrt fabs hypot pow sqrt erf erfc lgamma tgamma ceil floor nearbyint rint
    lrint llrint round lround llround trunc fmod remainder remquo copysign nan
    nextafter nexttoward fdim fmax fmin fma)
  foreach(suffix "" f l)
    list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.math.${name}${suffix})
  endforeach()
endforeach()

foreach(name cacos casin catan ccos csin ctan cacosh casinh catanh ccosh csinh
    ctanh cexp clog cabs cpow csqrt carg cimag conj cproj creal)
  foreach(suffix "" f l)
    list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.complex.${name}${suffix})
  endforeach()
endforeach()

foreach(name feclearexcept fegetexceptflag feraiseexcept fesetexceptflag
    fetestexcept fegetround fesetround fegetenv feholdexcept fesetenv feupdateenv)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.fenv.${name})
endforeach()
list(REMOVE_DUPLICATES TARGET_LIBC_ENTRYPOINTS)

include(${CMAKE_CURRENT_LIST_DIR}/MathLinkerScript.cmake)
