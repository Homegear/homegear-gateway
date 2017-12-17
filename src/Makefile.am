AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++11 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear

bin_PROGRAMS = homegear-gateway
homegear_gateway_SOURCES = main.cpp RpcServer.cpp Settings.cpp GD.cpp UPnP.cpp Families/EnOcean.cpp Families/HomeMaticCoc.cpp Families/ICommunicationInterface.cpp
homegear_gateway_LDADD = -lpthread -lhomegear-base -lgcrypt -lgnutls

if BSDSYSTEM
else
homegear_gateway_LDADD += -ldl
endif
