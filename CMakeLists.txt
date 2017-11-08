cmake_minimum_required(VERSION 3.8)
project(homegear_gateway)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/GD.cpp
        src/GD.h
        src/main.cpp
        src/RpcServer.cpp
        src/RpcServer.h
        src/Settings.cpp
        src/Settings.h
        config.h src/Families/EnOcean.cpp src/Families/EnOcean.h src/Families/ICommunicationInterface.cpp src/Families/ICommunicationInterface.h)

add_custom_target(homegear-gateway COMMAND ../makeDebug.sh SOURCES ${SOURCE_FILES})

add_library(homegear_gateway ${SOURCE_FILES})