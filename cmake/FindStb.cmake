find_path(
    Stb_INCLUDE_DIR
    NAMES stb_image.h
    PATHS
        "${PROJECT_SOURCE_DIR}/third_party/stb"
        "${PROJECT_SOURCE_DIR}/third_party"
        /boot/system/develop/headers
        /system/develop/headers
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    Stb
    REQUIRED_VARS Stb_INCLUDE_DIR
)

mark_as_advanced(Stb_INCLUDE_DIR)
