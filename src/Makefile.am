AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++17 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear

bin_PROGRAMS = homegear-gateway
homegear_gateway_SOURCES = main.cpp RpcServer.cpp Settings.cpp Gd.cpp UPnP.cpp Families/Cc110LTest.cpp Families/EnOcean.cpp Families/HomeMaticCc1101.cpp Families/HomeMaticCulfw.cpp Families/ICommunicationInterface.cpp Families/MaxCc1101.cpp Families/MaxCulfw.cpp Families/ZWave.cpp Families/Zigbee.cpp
homegear_gateway_LDADD = -lpthread -lhomegear-base -lc1-net -lz -lgcrypt -lgnutls -lcurl-gnutls

if BSDSYSTEM
else
homegear_gateway_LDADD += -ldl
endif
