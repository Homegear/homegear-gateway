/* Copyright 2013-2019 Homegear GmbH
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

#ifndef RPCSERVER_H_
#define RPCSERVER_H_

#include <homegear-base/BaseLib.h>
#include "Families/ICommunicationInterface.h"

#include <sys/stat.h>

class RpcServer
{
public:
	RpcServer(BaseLib::SharedObjects* bl);
	virtual ~RpcServer();

	int32_t familyId();
	bool isUnconfigured() { return _unconfigured; }

	bool start();
	void stop();
    BaseLib::PVariable invoke(std::string methodName, BaseLib::PArray& parameters);

	void txTest();
private:
	BaseLib::SharedObjects* _bl = nullptr;

	std::shared_ptr<BaseLib::TcpSocket> _tcpServer;
	std::unique_ptr<BaseLib::Rpc::BinaryRpc> _binaryRpc;
    std::unique_ptr<BaseLib::Rpc::RpcEncoder> _rpcEncoder;
    std::unique_ptr<BaseLib::Rpc::RpcDecoder> _rpcDecoder;

	std::mutex _maintenanceThreadMutex;
	std::thread _maintenanceThread;

	std::atomic_bool _unconfigured;
	std::atomic_bool _stopped;
    std::atomic_bool _clientConnected;
    int32_t _clientId = 0;

    std::mutex _invokeMutex;
    std::mutex _requestMutex;
    std::atomic_bool _waitForResponse;
    std::condition_variable _requestConditionVariable;
    BaseLib::PVariable _rpcResponse;

    std::unique_ptr<ICommunicationInterface> _interface;

	BaseLib::PVariable configure(BaseLib::PArray& parameters);

	void restart();

	void newConnection(int32_t clientId, std::string address, uint16_t port);
	void packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet);
};

#endif
