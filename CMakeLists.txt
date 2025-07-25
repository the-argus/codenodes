cmake_minimum_required(VERSION 3.10)

project(codenodes CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(codenodes src/main.cpp)

find_package(Clang CONFIG REQUIRED)
if(Clang_FOUND)
    target_include_directories(codenodes PRIVATE ${CLANG_INCLUDE_DIRS})
    target_link_libraries(codenodes PRIVATE libclang)
else()
    message(FATAL_ERROR "LibClang not found!")
endif()

include(FetchContent)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG v5.5.4
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glaze)
target_link_libraries(codenodes PRIVATE glaze::glaze)

# try to do static linking to clang
# set_target_properties(codenodes PROPERTIES LINK_FLAGS "-static")
