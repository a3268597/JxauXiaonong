# Find SDL2_image
find_package(PkgConfig QUIET)
pkg_check_modules(PC_SDL2_IMAGE QUIET SDL2_image)

find_path(SDL2_image_INCLUDE_DIR
    NAMES SDL_image.h
    PATHS ${PC_SDL2_IMAGE_INCLUDEDIR} ${PC_SDL2_IMAGE_INCLUDE_DIRS}
    PATH_SUFFIXES SDL2
    HINTS /usr/include /usr/local/include
)

find_library(SDL2_image_LIBRARY
    NAMES SDL2_image
    PATHS ${PC_SDL2_IMAGE_LIBDIR} ${PC_SDL2_IMAGE_LIBRARY_DIRS}
    HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

if(SDL2_image_INCLUDE_DIR AND SDL2_image_LIBRARY)
    set(SDL2_image_FOUND TRUE)
    set(SDL2_image_LIBRARIES ${SDL2_image_LIBRARY})
    set(SDL2_image_INCLUDE_DIRS ${SDL2_image_INCLUDE_DIR})
    
    # 创建导入目标
    if(NOT TARGET SDL2_image::SDL2_image)
        add_library(SDL2_image::SDL2_image UNKNOWN IMPORTED)
        set_target_properties(SDL2_image::SDL2_image PROPERTIES
            IMPORTED_LOCATION "${SDL2_image_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_image_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(SDL2_image_INCLUDE_DIR SDL2_image_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2_image
    REQUIRED_VARS SDL2_image_LIBRARY SDL2_image_INCLUDE_DIR
    VERSION_VAR PC_SDL2_IMAGE_VERSION
)
