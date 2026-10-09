# Stock vcpkg arm64-osx plus a fixed minimum macOS. Without it vcpkg targets the
# build host, so a newer Mac would produce libraries older players cannot load.
# Keep in step with CMAKE_OSX_DEPLOYMENT_TARGET and packaging/macos/Info.plist.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 11.0)

# curl probes pipe2() by linking only, so a newer SDK enables it even though the
# symbol is missing before macOS 27. Keep curl on its pipe() + fcntl() path.
if(PORT STREQUAL "curl")
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DHAVE_PIPE2=0)
endif()
