cmake_minimum_required(VERSION 3.16)
project(emojify CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(EMOJIFY_NATIVE "Optimize for the build machine's CPU (-march=native)" ON)

find_package(Threads REQUIRED)

add_executable(emojify
  src/main.cpp
  src/emoji.cpp
  src/model.cpp
  src/output.cpp
  src/stb_impl.cpp
)
target_include_directories(emojify PRIVATE third_party)
target_link_libraries(emojify PRIVATE Threads::Threads)

if(MSVC)
  target_compile_options(emojify PRIVATE /W3 $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(emojify PRIVATE -Wall -Wextra $<$<CONFIG:Release>:-O3>)
  if(EMOJIFY_NATIVE)
    target_compile_options(emojify PRIVATE -march=native)
  endif()
  # vendored single-header libraries are not warning-clean
  set_source_files_properties(src/stb_impl.cpp src/output.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

enable_testing()
add_executable(emojify_tests tests/emoji_test.cpp src/emoji.cpp src/stb_impl.cpp)
target_include_directories(emojify_tests PRIVATE third_party src)
target_link_libraries(emojify_tests PRIVATE Threads::Threads)
add_test(NAME emojify_tests COMMAND emojify_tests)
