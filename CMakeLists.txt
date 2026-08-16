cmake_minimum_required(VERSION 3.10)
project(jettyCat)

set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -g -fPIC -ggdb -std=c++20 -Wall -Wno-deprecated  -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations")

link_directories(/usr/local/lib)

option(BUILD_FUNC_TESTS "Build test executables" OFF)




find_package(yaml-cpp REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

find_library(MARIADB_LIB mariadb /usr/local/lib/mariadb)
find_library(HIREDIS_LIB hiredis /usr/local/lib)
find_library(SYLAR_LIB sylar /usr/local/lib)

file(GLOB_RECURSE ALL_SOURCES "code/*.cc")

add_library(jtcat STATIC 
    ${ALL_SOURCES}
)

set ( LIB_LIB
    pthread 
    dl
    yaml-cpp::yaml-cpp
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SYLAR_LIB}
    ${MARIADB_LIB}
    ${HIREDIS_LIB}
)
target_link_libraries(jtcat PUBLIC ${LIB_LIB} sylar)

target_include_directories(jtcat PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/code
    /usr/local/include/sylar
)

add_executable(jettyCat code/gate.cc
        code/dao/FriendDao.cc
)
add_dependencies(jettyCat jtcat)
target_link_libraries(jettyCat jtcat sylar)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)



# 子测试
add_subdirectory(${PROJECT_SOURCE_DIR}/test/function)
