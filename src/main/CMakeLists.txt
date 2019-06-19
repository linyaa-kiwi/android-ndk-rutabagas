# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -u ANativeActivity_onCreate")

include_directories(
    "${ANDROID_NDK}/sources/android/native_app_glue"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}" # for generated spvnum files
)

ru_add_spvnum(quad.vert.spvnum quad.vert.glsl)
ru_add_spvnum(quad.frag.spvnum quad.frag.glsl)

add_library(ru-main SHARED
   ru_app.c
   ru_media.c
   ru_rend.c
   quad.vert.spvnum
   quad.frag.spvnum
)

target_link_libraries(ru-main
   android
   android_native_app_glue
   log
   mediandk
   vulkan
   ru-util
)
