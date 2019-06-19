include_directories(
    "${ANDROID_NDK}/sources/android/native_app_glue"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(ru-util STATIC
   alloc.c
   check.c
   ru_chan.c
   ru_ndk.c
   ru_queue.c
)
