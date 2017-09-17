/* Copyright 2013-2017 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Homegear.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU Lesser General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
*/

#include "GD/GD.h"
#include "RpcServer.h"

RpcServer::RpcServer(BaseLib::SharedObjects* bl)
{
	_bl = bl;
}

RpcServer::~RpcServer()
{

}

void RpcServer::start()
{
	BaseLib::TcpSocket::TcpServerInfo serverInfo;
	serverInfo.maxConnections = 1;
	serverInfo.certFile = GD::settings.certPath();
	serverInfo.keyFile = GD::settings.keyPath();
	serverInfo.dhParamFile = GD::settings.dhPath();
	serverInfo.caFile = GD::settings.caFile();
	serverInfo.requireClientCert = true;
    serverInfo.newConnectionCallback = std::bind(&newConnection, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    serverInfo.packetReceivedCallback = std::bind(&packetReceived, std::placeholders::_1, std::placeholders::_2);

    _tcpServer = std::make_shared<BaseLib::TcpSocket>(_bl, serverInfo);
    std::string boundAddress;
    _tcpServer->startServer(GD::settings.listenAddress(), std::to_string(GD::settings.port()), boundAddress);
}

void RpcServer::stop()
{
	_tcpServer->stopServer();
	_tcpServer->waitForServerStopped();
}

void RpcServer::newConnection(int32_t clientId, std::string address, uint16_t port)
{

}

void RpcServer::packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet)
{
    std::vector<uint8_t> response;
    response.push_back('R');
    response.push_back(':');
    response.push_back(' ');
    response.insert(response.end(), packet.begin(), packet.end());
    _tcpServer->sendToClient(clientId, response);
}
