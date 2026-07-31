cmake_minimum_required(VERSION 3.21)

project(
  zimg_f64_reference
  VERSION 0.1.0
  DESCRIPTION "Binary64 reference path for pinned zimg Spline36 semantics"
  LANGUAGES CXX
)

option(ZIMG_F64_BUILD_MPFR_ORACLE "Build the optional MPFR-256 oracle" ON)
option(ZIMG_F64_ENABLE_SANITIZERS "Enable address/undefined/integer sanitizers" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(
  zimg_f64_filter
  STATIC
  src/zimg/resize/filter.cpp
  src/zimg/common/libm_wrapper.cpp
  src/zimg/common/matrix.cpp
)
target_include_directories(
  zimg_f64_filter
  PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/zimg"
)

add_executable(f64resize_yuv src/f64app/f64resize_yuv.cpp)
target_link_libraries(f64resize_yuv PRIVATE zimg_f64_filter Threads::Threads)

add_executable(f64_reference_probe src/f64app/reference_probe.cpp)
target_link_libraries(f64_reference_probe PRIVATE zimg_f64_filter)

function(zimg_f64_apply_contract target)
  if(MSVC)
    target_compile_options(
      "${target}"
      PRIVATE
      /O2
      /fp:strict
      /arch:AVX2
      /W4
      /permissive-
      # Pinned upstream Exception privately derives from std::runtime_error.
      # MSVC C4670/C4673 incorrectly diagnoses its public derived exceptions;
      # the fork catches the public zimg::error::Exception base.
      /wd4670
      /wd4673
    )
  else()
    target_compile_options(
      "${target}"
      PRIVATE
      -O3
      -mavx2
      -fno-fast-math
      -ffp-contract=off
      -frounding-math
      -fno-associative-math
      -fno-finite-math-only
      -Wall
      -Wextra
      -Wpedantic
    )
  endif()

  if(ZIMG_F64_ENABLE_SANITIZERS)
    if(MSVC)
      target_compile_options("${target}" PRIVATE /fsanitize=address)
      target_link_options("${target}" PRIVATE /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(
        "${target}" PRIVATE -fsanitize=address,undefined,integer -fno-omit-frame-pointer
      )
      target_link_options("${target}" PRIVATE -fsanitize=address,undefined,integer)
    else()
      target_compile_options(
        "${target}" PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
      )
      target_link_options("${target}" PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

zimg_f64_apply_contract(zimg_f64_filter)
zimg_f64_apply_contract(f64resize_yuv)
zimg_f64_apply_contract(f64_reference_probe)

if(ZIMG_F64_BUILD_MPFR_ORACLE)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(MPFR QUIET IMPORTED_TARGET mpfr)
  endif()

  if(TARGET PkgConfig::MPFR)
    add_executable(f64_mpfr_oracle src/f64app/mpfr_filter_oracle.cpp)
    target_link_libraries(f64_mpfr_oracle PRIVATE zimg_f64_filter PkgConfig::MPFR)
    zimg_f64_apply_contract(f64_mpfr_oracle)
  else()
    message(
      WARNING
      "MPFR was not found; f64_mpfr_oracle will not be built. "
      "Set ZIMG_F64_BUILD_MPFR_ORACLE=OFF to acknowledge this explicitly."
    )
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS f64resize_yuv f64_reference_probe RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(TARGET f64_mpfr_oracle)
  install(TARGETS f64_mpfr_oracle RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
