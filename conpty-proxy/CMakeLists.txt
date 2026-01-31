cmake_minimum_required(VERSION 3.20)

project(conpty_proxy)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(${PROJECT_NAME} conpty_proxy.c)

if(MSVC)
  add_compile_options(/W4 /utf8)
  set_property(TARGET ${PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -municode")
endif()


