/* Copyright 2013-2017 Sathya Laufer
*
* Homegear is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Homegear is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
*
* In addition, as a special exception, the copyright holders give
* permission to link the code of portions of this program with the
* OpenSSL library under certain conditions as described in each
* individual source file, and distribute linked combinations
* including the two.
* You must obey the GNU General Public License in all respects
* for all of the code used other than OpenSSL.  If you modify
* file(s) with this exception, you may extend this exception to your
* version of the file(s), but you are not obligated to do so.  If you
* do not wish to do so, delete this exception statement from your
* version.  If you delete this exception statement from all source
* files in the program, then also delete it here.
*/

#include "UPnP.h"
#include "GD.h"
#include "../config.h"

UPnP::UPnP()
{
	try
	{
		_stopServer = true;
		_serverSocketDescriptor.reset(new BaseLib::FileDescriptor());
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

UPnP::~UPnP()
{
	try
	{
		stop();
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::start()
{
	try
	{
		stop();

        if(_address.empty()) getAddress();
        if(_address.empty())
        {
            GD::out.printError("Error: UPnP Server: Could not get IP address.");
            return;
        }
        if(GD::settings.upnpUdn().empty())
        {
            GD::out.printError("Error: UPnP Server: Could not get UDN.");
            return;
        }
        _st = "uuid:" + GD::settings.upnpUdn();

        setPackets();

		_stopServer = false;
		GD::bl->threadManager.start(_listenThread, true, &UPnP::listen, this);
		sendNotify();
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::stop()
{
	try
	{
		if(_stopServer) return;
		_stopServer = true;
		GD::bl->threadManager.join(_listenThread);
		sendByebye();
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::getAddress()
{
	try
	{
		if(!GD::settings.upnpIpAddress().empty() && !BaseLib::Net::isIp(GD::settings.upnpIpAddress()))
		{
			//Assume address is interface name
			_address = BaseLib::Net::getMyIpAddress(GD::settings.upnpIpAddress());
		}
		else if(GD::settings.upnpIpAddress().empty() || GD::settings.upnpIpAddress() == "0.0.0.0" || GD::settings.upnpIpAddress() == "::")
		{
			_address = BaseLib::Net::getMyIpAddress();
			if(_address.empty()) GD::out.printError("Error: No IP address could be found to bind the server to. Please specify the IP address manually in main.conf.");
		}
		else _address = GD::settings.upnpIpAddress();
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::listen()
{
	try
	{
		getSocketDescriptor();
		GD::out.printInfo("Info: UPnP server started listening.");

		_lastAdvertisement = BaseLib::HelperFunctions::getTimeSeconds();
		char buffer[1024];
		int32_t bytesReceived = 0;
		struct sockaddr_in si_other;
		socklen_t slen = sizeof(si_other);
		fd_set readFileDescriptor;
		timeval timeout;
		int32_t nfds = 0;
		BaseLib::Http http;
		while(!_stopServer)
		{
			try
			{
				if(!_serverSocketDescriptor || _serverSocketDescriptor->descriptor == -1)
				{
					if(_stopServer) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(5000));
					getSocketDescriptor();
					continue;
				}

				timeout.tv_sec = 0;
				timeout.tv_usec = 100000;
				FD_ZERO(&readFileDescriptor);
				{
					auto fileDescriptorGuard = GD::bl->fileDescriptorManager.getLock();
					fileDescriptorGuard.lock();
					nfds = _serverSocketDescriptor->descriptor + 1;
					if(nfds <= 0)
					{
						fileDescriptorGuard.unlock();
						GD::out.printError("Error in UPnP Server: Socket closed (1).");
						GD::bl->fileDescriptorManager.shutdown(_serverSocketDescriptor);
					}
					FD_SET(_serverSocketDescriptor->descriptor, &readFileDescriptor);
				}

				bytesReceived = select(nfds, &readFileDescriptor, NULL, NULL, &timeout);
				if(bytesReceived == 0)
				{
					if(BaseLib::HelperFunctions::getTimeSeconds() - _lastAdvertisement >= 60) sendNotify();
					continue;
				}
				if(bytesReceived != 1)
				{
					GD::out.printError("Error in UPnP Server: Socket closed (2).");
					GD::bl->fileDescriptorManager.shutdown(_serverSocketDescriptor);
				}

				bytesReceived = recvfrom(_serverSocketDescriptor->descriptor, buffer, 1024, 0, (struct sockaddr *)&si_other, &slen);
				if(bytesReceived <= 0) continue;
				http.reset();
				http.process(buffer, bytesReceived, false);
				if(http.isFinished()) processPacket(http);
			}
            catch(BaseLib::Exception& ex)
            {
                GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(const std::exception& ex)
            {
                GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(...)
            {
                GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
            }
		}
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
	GD::bl->fileDescriptorManager.shutdown(_serverSocketDescriptor);
}

void UPnP::processPacket(BaseLib::Http& http)
{
	try
	{
		BaseLib::Http::Header& header = http.getHeader();
		if(header.method != "M-SEARCH" || header.fields.find("st") == header.fields.end() || header.host.empty()) return;

		BaseLib::HelperFunctions::toLower(header.fields.at("st"));
		if(header.fields.at("st") == "urn:schemas-upnp-org:device:basic:1" || header.fields.at("st") == "urn:schemas-upnp-org:device:basic:1.0" || header.fields.at("st") == "ssdp:all" || header.fields.at("st") == "upnp:rootdevice" || header.fields.at("st") == _st)
		{
			if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Discovery packet received from " + header.host);
			std::pair<std::string, std::string> address = BaseLib::HelperFunctions::splitLast(header.host, ':');
			int32_t port = BaseLib::Math::getNumber(address.second, false);
			if(!address.first.empty() && port > 0)
			{
				int32_t mx = 500;
				if(header.fields.find("mx") != header.fields.end()) mx = BaseLib::Math::getNumber(header.fields.at("mx"), false) * 1000;
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				//Wait for 0 to mx seconds for load balancing
				if(mx > 500)
				{
					mx = BaseLib::HelperFunctions::getRandomNumber(0, mx - 500);
					GD::out.printDebug("Debug: Sleeping " + std::to_string(mx) + "ms before sending response.");
					std::this_thread::sleep_for(std::chrono::milliseconds(mx));
				}
				sendOK(address.first, port, header.fields.at("st") == "upnp:rootdevice");
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				sendNotify();
			}
		}
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::setPackets()
{
	try
	{
        std::string notifyPacketBase = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nCACHE-CONTROL: max-age=1800\r\nSERVER: Homegear Gateway " + std::string(VERSION) + "\r\nLOCATION: " + "binrpcs://" + _address + ":" + std::to_string(GD::settings.port()) + "/\r\nHG-FAMILY-ID: " + std::to_string(GD::rpcServer->familyId()) + "\r\n";;
        std::string alivePacketRoot = notifyPacketBase + "NT: upnp:rootdevice\r\nUSN: " + _st + "::upnp:rootdevice\r\nNTS: ssdp:alive\r\n\r\n";
        std::string alivePacketRootUUID = notifyPacketBase + "NT: " + _st + "\r\nUSN: " + _st + "\r\nNTS: ssdp:alive\r\n\r\n";
        std::string alivePacket = notifyPacketBase + "NT: urn:schemas-upnp-org:device:basic:1\r\nUSN: " + _st + "\r\nNTS: ssdp:alive\r\n\r\n";
        _packets.notifyRoot = std::vector<char>(&alivePacketRoot.at(0), &alivePacketRoot.at(0) + alivePacketRoot.size());
        _packets.notifyRootUUID = std::vector<char>(&alivePacketRootUUID.at(0), &alivePacketRootUUID.at(0) + alivePacketRootUUID.size());
        _packets.notify = std::vector<char>(&alivePacket.at(0), &alivePacket.at(0) + alivePacket.size());

        std::string byebyePacketRoot = notifyPacketBase + "NT: upnp:rootdevice\r\nUSN: " + _st + "::upnp:rootdevice\r\nNTS: ssdp:byebye\r\n\r\n";
        std::string byebyePacketRootUUID = notifyPacketBase + "NT: " + _st + "\r\nUSN: " + _st + "\r\nNTS: ssdp:byebye\r\n\r\n";
        std::string byebyePacket = notifyPacketBase + "NT: urn:schemas-upnp-org:device:basic:1\r\nUSN: " + _st + "\r\nNTS: ssdp:byebye\r\n\r\n";
        _packets.byebyeRoot = std::vector<char>(&byebyePacketRoot.at(0), &byebyePacketRoot.at(0) + byebyePacketRoot.size());
        _packets.byebyeRootUUID = std::vector<char>(&byebyePacketRootUUID.at(0), &byebyePacketRootUUID.at(0) + byebyePacketRootUUID.size());
        _packets.byebye = std::vector<char>(&byebyePacket.at(0), &byebyePacket.at(0) + byebyePacket.size());

        std::string okPacketBase = std::string("HTTP/1.1 200 OK\r\nCache-Control: max-age=1800\r\nLocation: ") + "binrpcs://" + _address + ":" + std::to_string(GD::settings.port()) + "/\r\nServer: Homegear Gateway " + std::string(VERSION) + "\r\nHG-Family-ID: " + std::to_string(GD::rpcServer->familyId()) + "\r\n";
        std::string okPacketRoot = okPacketBase + "ST: upnp:rootdevice\r\nUSN: " + _st + "::upnp:rootdevice\r\n\r\n";
        std::string okPacketRootUUID = okPacketBase + "ST: " + _st + "\r\nUSN: " + _st + "\r\n\r\n";
        std::string okPacket = okPacketBase + "ST: urn:schemas-upnp-org:device:basic:1\r\nUSN: " + _st + "\r\n\r\n";
        _packets.okRoot = std::vector<char>(&okPacketRoot.at(0), &okPacketRoot.at(0) + okPacketRoot.size());
        _packets.okRootUUID = std::vector<char>(&okPacketRootUUID.at(0), &okPacketRootUUID.at(0) + okPacketRootUUID.size());
        _packets.ok = std::vector<char>(&okPacket.at(0), &okPacket.at(0) + okPacket.size());
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::sendOK(std::string destinationIpAddress, int32_t destinationPort, bool rootDeviceOnly)
{
	try
	{
		if(!_serverSocketDescriptor || _serverSocketDescriptor->descriptor == -1) return;
		struct sockaddr_in addessInfo;
		addessInfo.sin_family = AF_INET;
		addessInfo.sin_addr.s_addr = inet_addr(destinationIpAddress.c_str());
		addessInfo.sin_port = htons(destinationPort);

        if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Sending discovery response packets to " + destinationIpAddress + " on port " + std::to_string(destinationPort));
        if(sendto(_serverSocketDescriptor->descriptor, _packets.okRoot.data(), _packets.okRoot.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }
        if(!rootDeviceOnly)
        {
            if(sendto(_serverSocketDescriptor->descriptor, _packets.okRootUUID.data(), _packets.okRootUUID.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
            {
                GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
            }
            if(sendto(_serverSocketDescriptor->descriptor, _packets.ok.data(), _packets.ok.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
            {
                GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
            }
        }
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::sendNotify()
{
	try
	{
		if(!_serverSocketDescriptor || _serverSocketDescriptor->descriptor == -1) return;
		struct sockaddr_in addessInfo;
		addessInfo.sin_family = AF_INET;
		addessInfo.sin_addr.s_addr = inet_addr("239.255.255.250");
		addessInfo.sin_port = htons(1900);

        if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Sending notify packets.");
        if(sendto(_serverSocketDescriptor->descriptor, _packets.notifyRoot.data(), _packets.notifyRoot.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }
        if(sendto(_serverSocketDescriptor->descriptor, _packets.notifyRootUUID.data(), _packets.notifyRootUUID.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }
        if(sendto(_serverSocketDescriptor->descriptor, _packets.notify.data(), _packets.notify.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }

		_lastAdvertisement = BaseLib::HelperFunctions::getTimeSeconds();
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::sendByebye()
{
	try
	{
		if(!_serverSocketDescriptor || _serverSocketDescriptor->descriptor == -1) return;
		struct sockaddr_in addessInfo;
		addessInfo.sin_family = AF_INET;
		addessInfo.sin_addr.s_addr = inet_addr("239.255.255.250");
		addessInfo.sin_port = htons(1900);

        if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Sending byebye packets.");
        if(sendto(_serverSocketDescriptor->descriptor, _packets.byebyeRoot.data(), _packets.byebyeRoot.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }
        if(sendto(_serverSocketDescriptor->descriptor, _packets.byebyeRootUUID.data(), _packets.byebyeRootUUID.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }
        if(sendto(_serverSocketDescriptor->descriptor, _packets.byebye.data(), _packets.byebye.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
        {
            GD::out.printWarning("Warning: Error sending packet in UPnP server: " + std::string(strerror(errno)));
        }
	}
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void UPnP::getSocketDescriptor()
{
	try
	{
		if(_address.empty()) return;
		_serverSocketDescriptor = GD::bl->fileDescriptorManager.add(socket(AF_INET, SOCK_DGRAM, 0));
		if(_serverSocketDescriptor->descriptor == -1)
		{
			GD::out.printError("Error: Could not create socket.");
			return;
		}

		int32_t reuse = 1;
		if(setsockopt(_serverSocketDescriptor->descriptor, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) == -1)
		{
			GD::out.printWarning("Warning: Could not set socket options in UPnP server: " + std::string(strerror(errno)));
		}

		GD::out.printInfo("Info: UPnP server: Binding to address: " + _address);

		char loopch = 0;
		if(setsockopt(_serverSocketDescriptor->descriptor, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) == -1)
		{
			GD::out.printWarning("Warning: Could not set socket options in UPnP server: " + std::string(strerror(errno)));
		}

		struct in_addr localInterface;
		localInterface.s_addr = inet_addr(_address.c_str());
		if(setsockopt(_serverSocketDescriptor->descriptor, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface)) == -1)
		{
			GD::out.printWarning("Warning: Could not set socket options in UPnP server: " + std::string(strerror(errno)));
		}

		struct sockaddr_in localSock;
		memset((char *) &localSock, 0, sizeof(localSock));
		localSock.sin_family = AF_INET;
		localSock.sin_port = htons(1900);
		localSock.sin_addr.s_addr = inet_addr("239.255.255.250");

		if(bind(_serverSocketDescriptor->descriptor, (struct sockaddr*)&localSock, sizeof(localSock)) == -1)
		{
			GD::out.printError("Error: Binding to address " + _address + " failed: " + std::string(strerror(errno)));
			GD::bl->fileDescriptorManager.close(_serverSocketDescriptor);
			return;
		}

		struct ip_mreq group;
		group.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
		group.imr_interface.s_addr = inet_addr(_address.c_str());
		if(setsockopt(_serverSocketDescriptor->descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group, sizeof(group)) == -1)
		{
            GD::out.printWarning("Warning: Could not set socket options in UPnP server: " + std::string(strerror(errno)));
		}
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(const std::exception& ex)
	{
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}