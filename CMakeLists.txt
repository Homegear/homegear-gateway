cmake_minimum_required(VERSION 3.8)
project(homegear_gateway)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/Gd.cpp
        src/Gd.h
        src/main.cpp
        src/RpcServer.cpp
        src/RpcServer.h
        src/Settings.cpp
        src/Settings.h
        config.h
        src/Families/Cc110LTest.cpp
        src/Families/Cc110LTest.h
        src/Families/EnOcean.cpp
        src/Families/EnOcean.h
        src/Families/HomeMaticCc1101.cpp
        src/Families/HomeMaticCc1101.h
        src/Families/HomeMaticCulfw.cpp
        src/Families/HomeMaticCulfw.h
        src/Families/ICommunicationInterface.cpp
        src/Families/ICommunicationInterface.h
        src/Families/MaxCc1101.cpp
        src/Families/MaxCc1101.h
        src/Families/MaxCulfw.cpp
        src/Families/MaxCulfw.h
        src/Families/ZWave.cpp
        src/Families/ZWave.h
	src/Families/Zigbee.cpp
	src/Families/Zigbee.h
        src/UPnP.cpp
        src/UPnP.h)

add_custom_target(homegear-gateway COMMAND ../makeDebug.sh SOURCES ${SOURCE_FILES})

add_library(homegear_gateway ${SOURCE_FILES})