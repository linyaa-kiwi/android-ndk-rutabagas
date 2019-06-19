include_directories(
    "${ANDROID_NDK}/sources/android/native_app_glue"
)

add_library(android_native_app_glue STATIC
   "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
)

set_property(
    TARGET android_native_app_glue

    # app_dummy() triggers -Wno-missing-prototypes
    PROPERTY COMPILE_OPTIONS "-Wno-missing-prototypes"
)
