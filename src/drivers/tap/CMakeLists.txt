add_library(driver_tap STATIC tap_device.c)
target_include_directories(driver_tap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})