# Storage mode does not imply public thread creation or concurrent libc support.
option(LIBC_MMIX_BUILD_SHARED_LIBC "Build the experimental Linux shared libc provider" OFF)
set(mmix_runtime_tls_model local-exec)
if(LIBC_MMIX_BUILD_SHARED_LIBC)
  set(mmix_runtime_tls_model initial-exec)
endif()
option(LIBC_MMIX_ENABLE_STATIC_TLS "Build the main-thread static TLS profile" OFF)
option(LIBC_MMIX_BUILD_THREAD_LIFECYCLE
  "Build the non-installed internal Linux thread lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_CREATION
  "Add public pthread creation and identity to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE
  "Add public pthread join, detach and C thread exit" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_MUTEX
  "Add public private normal mutexes to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_MUTEX_TYPES
  "Admit private recursive and error-checking pthread mutexes" OFF)
option(LIBC_MMIX_BUILD_TIMED_MUTEX
  "Add absolute realtime and monotonic pthread mutex waits" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_RWLOCK
  "Add private pthread read-write locks and absolute waits" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_BARRIER
  "Add private pthread barriers and sharing attributes" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_SPIN
  "Add private pthread spinlocks with atomic ownership metadata" OFF)
option(LIBC_MMIX_BUILD_PROCESS_SHARED
  "Admit process-shared pthread synchronization" OFF)
option(LIBC_MMIX_BUILD_ROBUST_MUTEX
  "Admit Linux robust pthread mutex ownership and recovery" OFF)
option(LIBC_MMIX_BUILD_PI_MUTEX
  "Admit Linux priority-inheritance pthread mutexes" OFF)
option(LIBC_MMIX_BUILD_PROTECT_MUTEX
  "Admit FIFO/RR pthread priority-ceiling protection" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_SCHEDULING
  "Build MMIX Linux pthread scheduling interfaces" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_CLOCKS
  "Add leased Linux thread CPU clock acquisition" OFF)
option(LIBC_MMIX_BUILD_CONCURRENCY_HINTS
  "Add process-wide pthread concurrency hints" OFF)
option(LIBC_MMIX_BUILD_COORDINATED_FORK
  "Add bounded coordinated fork and exec-only child recovery" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_COND
  "Add public private condition variables to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_ONCE
  "Add public C once initialization to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_TSS
  "Add public thread-specific keys to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_SIGNALS
  "Add public thread mask and directed signal operations to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_CONCURRENT_ALLOCATION
  "Add concurrent allocation to the public lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_CONCURRENT_STDIO
  "Add concurrent file streams to the public lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_THREADED_LIBC
  "Compose the bounded public Linux pthread and concurrent libc archive" OFF)
option(LIBC_MMIX_BUILD_UNWIND_DIAGNOSTICS
  "Add diagnostic providers for Linux threaded unwind preparation" OFF)
option(LIBC_MMIX_BUILD_UNWIND_ROOTS
  "Prepare owning Linux thread unwind roots without public forced exit" OFF)
option(LIBC_MMIX_BUILD_FORCED_EXIT
  "Use forced unwinding for explicit Linux pthread exit" OFF)
option(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION
  "Prepare explicit deferred cancellation polling without blocking points" OFF)
option(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS
  "Prepare deferred cancellation windows for public Linux syscalls" OFF)
option(LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS
  "Prepare cancellation-safe stream leaves and runtime cleanup" OFF)
option(LIBC_MMIX_BUILD_ASYNC_CANCELLATION
  "Prepare asynchronous cancellation with explicit registered cleanup" OFF)
option(LIBC_MMIX_BUILD_EXTENDED_PTHREAD
  "Compose one extended pthread runtime with both cancellation modes" OFF)
option(LIBC_MMIX_BUILD_TLS_DESTRUCTORS
  "Prepare dynamically allocated per-thread static TLS destructor registration" OFF)
option(LIBC_MMIX_BUILD_CXX_RUNTIME_DEPENDENCIES
  "Add libc dependencies for native threaded C++ runtime preparation" OFF)
option(LIBC_MMIX_BUILD_SINGLE_THREADED_LIBC
  "Compose the single-thread Linux libc and C runtime" OFF)
include(${CMAKE_CURRENT_LIST_DIR}/UnixComponents.cmake)
if(LIBC_MMIX_BUILD_SINGLE_THREADED_LIBC)
  # Preserve the existing C runtime targets, manifests and installation inputs.
  set(LIBC_MMIX_BUILD_C_RUNTIME ON)
endif()
function(mmix_check_runtime_mode)
  if(LIBC_MMIX_BUILD_SHARED_LIBC AND
     (NOT LIBC_MMIX_BUILD_EXTENDED_PTHREAD OR
      NOT CMAKE_POSITION_INDEPENDENT_CODE OR
      LIBC_MMIX_INSTALL_THREADED_RUNTIME OR LIBC_MMIX_INSTALL_RUNTIME))
    message(FATAL_ERROR "MMIX shared libc requires the PIC extended pthread build without static installation")
  endif()
  if(LIBC_MMIX_BUILD_EXTENDED_PTHREAD)
    foreach(component THREADED_LIBC MUTEX_TYPES TIMED_MUTEX PTHREAD_RWLOCK
                      PTHREAD_BARRIER PTHREAD_SPIN PROCESS_SHARED ROBUST_MUTEX
                      PI_MUTEX PROTECT_MUTEX PTHREAD_SCHEDULING PTHREAD_CLOCKS
                      CONCURRENCY_HINTS COORDINATED_FORK DEFERRED_CANCELLATION
                      CANCELLATION_WINDOWS CANCELLATION_RUNTIME_LOCKS
                      ASYNC_CANCELLATION CXX_RUNTIME_DEPENDENCIES)
      if(NOT LIBC_MMIX_BUILD_${component})
        message(FATAL_ERROR "MMIX extended pthread requires ${component}")
      endif()
    endforeach()
  endif()
  if(LIBC_MMIX_BUILD_COORDINATED_FORK AND
     (NOT LIBC_MMIX_BUILD_THREADED_LIBC OR NOT LIBC_MMIX_BUILD_PI_MUTEX OR
      NOT LIBC_MMIX_BUILD_PTHREAD_ONCE OR
      (LIBC_MMIX_BUILD_ASYNC_CANCELLATION AND NOT LIBC_MMIX_BUILD_EXTENDED_PTHREAD)))
    message(FATAL_ERROR "MMIX coordinated fork requires threaded libc, PI and once in a deferred-only profile")
  endif()
  if((LIBC_MMIX_BUILD_PTHREAD_CLOCKS OR LIBC_MMIX_BUILD_CONCURRENCY_HINTS)
     AND (NOT LIBC_MMIX_BUILD_PTHREAD_CREATION OR NOT LIBC_MMIX_BUILD_THREADED_LIBC))
    message(FATAL_ERROR "MMIX thread clocks and hints require the public threaded libc profile")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_SCHEDULING AND
     (NOT LIBC_MMIX_BUILD_PI_MUTEX OR NOT LIBC_MMIX_BUILD_PTHREAD_CREATION OR
      NOT LIBC_MMIX_BUILD_UNWIND_DIAGNOSTICS OR
      (LIBC_MMIX_BUILD_ASYNC_CANCELLATION AND NOT LIBC_MMIX_BUILD_EXTENDED_PTHREAD)))
    message(FATAL_ERROR "MMIX scheduling requires PI, creation and a deferred-only development profile")
  endif()
  if(LIBC_MMIX_BUILD_PROTECT_MUTEX AND
     (NOT LIBC_MMIX_BUILD_PI_MUTEX OR
      (LIBC_MMIX_BUILD_ASYNC_CANCELLATION AND NOT LIBC_MMIX_BUILD_EXTENDED_PTHREAD)))
    message(FATAL_ERROR "MMIX protection requires PI and a deferred-only profile")
  endif()
  if(LIBC_MMIX_BUILD_PI_MUTEX AND
     (NOT LIBC_MMIX_BUILD_ROBUST_MUTEX OR NOT LIBC_MMIX_BUILD_TIMED_MUTEX))
    message(FATAL_ERROR "MMIX PI mutexes require robust and timed mutex profiles")
  endif()
  if(LIBC_MMIX_BUILD_ROBUST_MUTEX AND
     (NOT LIBC_MMIX_BUILD_PROCESS_SHARED OR NOT LIBC_MMIX_BUILD_CANCELLATION_WINDOWS))
    message(FATAL_ERROR "MMIX robust mutexes require shared synchronization and cancellation windows")
  endif()
  if(LIBC_MMIX_BUILD_PROCESS_SHARED)
    foreach(component PTHREAD_MUTEX MUTEX_TYPES PTHREAD_COND PTHREAD_RWLOCK PTHREAD_BARRIER PTHREAD_SPIN)
      if(NOT LIBC_MMIX_BUILD_${component})
        message(FATAL_ERROR "MMIX process sharing requires ${component}")
      endif()
    endforeach()
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_SPIN AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX spinlocks require the public thread profile")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_BARRIER AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX barriers require the public thread profile")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_RWLOCK AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX rwlocks require the public thread profile")
  endif()
  if(LIBC_MMIX_BUILD_TIMED_MUTEX AND NOT LIBC_MMIX_BUILD_MUTEX_TYPES)
    message(FATAL_ERROR "MMIX timed mutexes require the mutex types profile")
  endif()
  if(LIBC_MMIX_BUILD_MUTEX_TYPES AND NOT LIBC_MMIX_BUILD_PTHREAD_MUTEX)
    message(FATAL_ERROR "MMIX mutex types require the public mutex profile")
  endif()
  if(LIBC_MMIX_BUILD_ASYNC_CANCELLATION AND NOT LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    message(FATAL_ERROR "MMIX async cancellation requires cancellation windows")
  endif()
  if(LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS AND
     NOT LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    message(FATAL_ERROR "MMIX cancellation resource cleanup requires cancellation windows")
  endif()
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS AND NOT LIBC_MMIX_BUILD_DEFERRED_CANCELLATION)
    message(FATAL_ERROR "MMIX cancellation windows require deferred cancellation")
  endif()
  if(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION AND NOT LIBC_MMIX_BUILD_FORCED_EXIT)
    message(FATAL_ERROR "MMIX deferred cancellation requires forced pthread exit")
  endif()
  if(LIBC_MMIX_BUILD_FORCED_EXIT AND NOT LIBC_MMIX_BUILD_CXX_RUNTIME_DEPENDENCIES)
    message(FATAL_ERROR "MMIX forced pthread exit requires the native C++ runtime composition")
  endif()
  if(LIBC_MMIX_BUILD_CXX_RUNTIME_DEPENDENCIES AND NOT LIBC_MMIX_BUILD_TLS_DESTRUCTORS)
    message(FATAL_ERROR "MMIX C++ runtime support requires TLS destructors")
  endif()
  if(LIBC_MMIX_BUILD_TLS_DESTRUCTORS AND NOT LIBC_MMIX_BUILD_UNWIND_ROOTS)
    message(FATAL_ERROR "MMIX TLS destructors require owning unwind roots")
  endif()
  if(LIBC_MMIX_BUILD_UNWIND_ROOTS AND NOT LIBC_MMIX_BUILD_UNWIND_DIAGNOSTICS)
    message(FATAL_ERROR "MMIX unwind roots require threaded unwind diagnostics")
  endif()
  if(LIBC_MMIX_BUILD_UNWIND_DIAGNOSTICS AND NOT LIBC_MMIX_BUILD_THREADED_LIBC)
    message(FATAL_ERROR "MMIX unwind diagnostics require threaded libc")
  endif()
  if(LIBC_MMIX_BUILD_SINGLE_THREADED_LIBC)
    if(LIBC_MMIX_BUILD_THREADED_LIBC OR LIBC_MMIX_BUILD_THREAD_LIFECYCLE OR
       LIBC_MMIX_ENABLE_STATIC_TLS OR
       NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" OR
       NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_SHARED")
      message(FATAL_ERROR "MMIX single-thread libc requires SINGLE/shared storage without static TLS or thread composition")
    endif()
  endif()
  if(LIBC_MMIX_BUILD_THREADED_LIBC)
    foreach(component THREAD_LIFECYCLE RUNTIME_STATE PTHREAD_CREATION
                      PTHREAD_LIFECYCLE PTHREAD_MUTEX PTHREAD_COND PTHREAD_ONCE
                      PTHREAD_TSS PTHREAD_SIGNALS CONCURRENT_ALLOCATION
                      CONCURRENT_STDIO)
      if(NOT LIBC_MMIX_BUILD_${component})
        message(FATAL_ERROR "MMIX threaded libc requires ${component}")
      endif()
    endforeach()
  endif()
  if(LIBC_MMIX_BUILD_CONCURRENT_STDIO AND NOT LIBC_MMIX_BUILD_CONCURRENT_ALLOCATION)
    message(FATAL_ERROR "MMIX concurrent stdio requires concurrent allocation")
  endif()
  if(LIBC_MMIX_BUILD_CONCURRENT_ALLOCATION AND NOT LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE)
    message(FATAL_ERROR "MMIX concurrent allocation requires the public pthread lifecycle")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_SIGNALS AND NOT LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE)
    message(FATAL_ERROR "MMIX public thread signals require the public pthread lifecycle")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_TSS AND NOT LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE)
    message(FATAL_ERROR "MMIX public TSS requires the public pthread lifecycle")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_ONCE AND NOT LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE)
    message(FATAL_ERROR "MMIX public once requires the public pthread lifecycle")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_COND AND NOT LIBC_MMIX_BUILD_PTHREAD_MUTEX)
    message(FATAL_ERROR "MMIX public conditions require the public mutex profile")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_MUTEX AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX public mutexes require the pthread creation profile")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX public pthread lifecycle requires pthread creation")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_CREATION AND NOT LIBC_MMIX_BUILD_THREAD_LIFECYCLE)
    message(FATAL_ERROR "MMIX pthread creation requires the internal lifecycle")
  endif()
  if(LIBC_MMIX_BUILD_THREAD_LIFECYCLE)
    if(NOT LIBC_MMIX_ENABLE_STATIC_TLS OR NOT LIBC_MMIX_BUILD_RUNTIME_STATE)
      message(FATAL_ERROR "MMIX internal lifecycle requires static TLS and runtime state")
    endif()
    # Do not make unreviewed concurrent libc consumers reachable through this
    # preparation archive. Full public libc/pthread composition is separate.
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
      string(REGEX REPLACE "^LIBC_MMIX_BUILD_" "" component "${variable}")
      if(LIBC_MMIX_BUILD_THREADED_LIBC AND component IN_LIST MMIX_UNIX_COMPONENTS)
        continue()
      endif()
      # Nonlocal transfers use per-call storage and the calling thread's kernel
      # register-stack service; they do not introduce shared libc state.
      if(variable STREQUAL "LIBC_MMIX_BUILD_SHARED_LIBC" OR
         variable STREQUAL "LIBC_MMIX_BUILD_JUMP_SAVE")
        continue()
      endif()
      # Math providers use immutable tables, per-call scratch, thread-local
      # errno and the calling thread's floating-point environment.
      if(variable STREQUAL "LIBC_MMIX_BUILD_MATH")
        continue()
      endif()
      if(variable MATCHES "^LIBC_MMIX_BUILD_" AND ${variable} AND
         NOT variable MATCHES "^LIBC_MMIX_BUILD_(EXTENDED_PTHREAD|THREADED_LIBC|UNWIND_DIAGNOSTICS|UNWIND_ROOTS|FORCED_EXIT|DEFERRED_CANCELLATION|CANCELLATION_WINDOWS|CANCELLATION_RUNTIME_LOCKS|ASYNC_CANCELLATION|PROCESS_SHARED|ROBUST_MUTEX|PI_MUTEX|PROTECT_MUTEX|PTHREAD_SCHEDULING|PTHREAD_CLOCKS|CONCURRENCY_HINTS|COORDINATED_FORK|TLS_DESTRUCTORS|CXX_RUNTIME_DEPENDENCIES|THREAD_LIFECYCLE|RUNTIME_STATE|PTHREAD_CREATION|PTHREAD_LIFECYCLE|PTHREAD_MUTEX|MUTEX_TYPES|TIMED_MUTEX|PTHREAD_RWLOCK|PTHREAD_BARRIER|PTHREAD_SPIN|PTHREAD_COND|PTHREAD_ONCE|PTHREAD_TSS|PTHREAD_SIGNALS|CONCURRENT_ALLOCATION|CONCURRENT_STDIO)$")
        message(FATAL_ERROR "MMIX internal lifecycle does not support ${variable}")
      endif()
    endforeach()
  endif()
  if(LIBC_MMIX_ENABLE_STATIC_TLS)
    if(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM" OR
       NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_THREAD_LOCAL")
      message(FATAL_ERROR "MMIX static TLS requires PLATFORM storage and THREAD_LOCAL errno")
    endif()
    if(NOT "-ftls-model=${mmix_runtime_tls_model}" IN_LIST LIBC_COMPILE_OPTIONS_DEFAULT OR
       NOT "-fno-lto" IN_LIST LIBC_COMPILE_OPTIONS_DEFAULT)
      message(FATAL_ERROR "MMIX startup TLS requires native ${mmix_runtime_tls_model} runtime providers")
    endif()
    foreach(component FORK VFORK SPAWN ACCOUNT_MANAGEMENT SIGNAL_RUNTIME
                      COMMAND_ENVIRONMENT_RUNTIME)
      if(component STREQUAL "ACCOUNT_MANAGEMENT" AND LIBC_MMIX_BUILD_THREADED_LIBC)
        continue()
      endif()
      if(LIBC_MMIX_BUILD_${component})
        message(FATAL_ERROR "MMIX static TLS does not yet support ${component}")
      endif()
    endforeach()
    if(LIBC_MMIX_INSTALL_RUNTIME)
      message(FATAL_ERROR "MMIX static TLS is not an installed runtime profile")
    endif()
  elseif(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" OR
         NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_SHARED")
    message(FATAL_ERROR "MMIX Linux requires SINGLE/shared state or explicit static TLS")
  endif()
endfunction()
