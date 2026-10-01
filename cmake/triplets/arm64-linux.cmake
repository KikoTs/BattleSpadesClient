# Stock vcpkg arm64-linux (community triplet) plus one libsodium workaround.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# libsodium 1.0.21's ipcrypt_armcrypto.c passes uint8x16_t where NEON
# intrinsics take uint64x2_t. Clang converts implicitly; GCC rejects it
# unless lax vector conversions are allowed.
if(PORT STREQUAL "libsodium")
    set(VCPKG_C_FLAGS "-flax-vector-conversions")
    set(VCPKG_CXX_FLAGS "-flax-vector-conversions")
endif()
