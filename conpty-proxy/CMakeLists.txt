cmake_minimum_required(VERSION 3.20)

project(conpty_proxy)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(${PROJECT_NAME} conpty_proxy.c arena.c)

if(MSVC)
  add_compile_options(/W4 /utf8)
  set_property(TARGET ${PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -municode")
endif()

# Copy executable to parent directory after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  $<TARGET_FILE:${PROJECT_NAME}>
  ${CMAKE_CURRENT_SOURCE_DIR}/../conpty_proxy.exe
  COMMENT "Copying conpty_proxy.exe to source directory"
)

