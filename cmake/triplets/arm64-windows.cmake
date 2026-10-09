set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Keep native dependencies on the same compiler as the ARM64 release build.
# MSVC 14.51 emits a looping destructor return path in OpenAL 1.25.0.
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)

if(PORT STREQUAL "openal-soft")
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DALSOFT_ENABLE_MODULES=OFF)
endif()
