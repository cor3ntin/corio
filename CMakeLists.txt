cmake_minimum_required (VERSION 3.14)

project(corio)

file(GLOB_RECURSE FILES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.hpp
    ${PROJECT_SOURCE_DIR}/include/*.hpp
)

add_executable(corio ${FILES})
target_include_directories(corio PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/cmcstl2/include)
target_compile_options(corio PUBLIC -std=c++2a -Xclang -fconcepts-ts -stdlib=libc++ -lc++experimental)
target_link_options(corio PUBLIC -stdlib=libc++ -lc++experimental -pthread)