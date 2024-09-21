# Include GStreamer header files
include_directories(${GST_INCLUDE_DIRS})

# Add an executable for the project
add_executable(gstreamer_bandwidth_app JpegStreaming.c)

# Link GStreamer libraries
target_link_libraries(gstreamer_bandwidth_app ${GST_LIBRARIES})

# Add compile options (if needed)
target_compile_options(gstreamer_bandwidth_app PRIVATE ${GST_CFLAGS_OTHER})
