cmake_minimum_required(VERSION 3.10)
project(searchwx LANGUAGES CXX)

#set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

#find_package(PkgConfig REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "") #json 

# Native Linux build
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
add_executable(searchwx main.cpp)
target_link_libraries(searchwx ${wxWidgets_LIBRARIES})


