project(qcommontest LANGUAGES CXX)

cmake_minimum_required(VERSION 2.8.12)

find_package(Qt5Test REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_CXX_FLAGS "-O0 -g -fno-omit-frame-pointer -fsanitize=address -msse4.2")

enable_testing(true)

add_executable(
        qcommontest
        main.cpp
        "../configstringstorage.cpp"
        "../hash.cpp"
        "../wswfs.cpp"
	"../wswstringview.cpp"
        "../userinfo.cpp"
        boundsbuildertest.cpp
        bufferedreadertest.cpp
        configstringstoragetest.cpp
        freelistallocatortest.cpp
        demometadatatest.cpp
        fsutilstest.cpp
        enumtokenmatchertest.cpp
        staticstringtest.cpp
        stringspanstoragetest.cpp
        stringsplittertest.cpp
        stringviewtest.cpp
        tonumtest.cpp
        userinfotest.cpp)

add_test(NAME qcommontest COMMAND qcommontest)
set_property(TARGET qcommontest PROPERTY CXX_STANDARD 20)
target_link_libraries(qcommontest PRIVATE Qt5::Test)
