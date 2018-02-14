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

#include "RpcServer.h"
#include "GD.h"
#include "Families/EnOcean.h"
#include "Families/HomeMaticCulfw.h"

RpcServer::RpcServer(BaseLib::SharedObjects* bl)
{
    signal(SIGPIPE, SIG_IGN);

    _stopped = true;
    _clientConnected = false;
    _waitForResponse = false;

	_bl = bl;
    _binaryRpc.reset(new BaseLib::Rpc::BinaryRpc(bl));
    _rpcDecoder.reset(new BaseLib::Rpc::RpcDecoder(bl, false, false));
    _rpcEncoder.reset(new BaseLib::Rpc::RpcEncoder(bl, true, true));
}

RpcServer::~RpcServer()
{

}

bool RpcServer::start()
{
    try
    {
        if(GD::settings.family().empty())
        {
            GD::out.printError("Error: Setting family in gateway.conf is empty.");
            return false;
        }

        if(GD::settings.family() == "enocean") _interface = std::unique_ptr<EnOcean>(new EnOcean(_bl));
        else if(GD::settings.family() == "homematicculfw") _interface = std::unique_ptr<HomeMaticCulfw>(new HomeMaticCulfw(_bl));

        if(!_interface)
        {
            GD::out.printError("Error: Unknown family: " + GD::settings.family() + ". Please correct it in gateway.conf.");
            return false;
        }

        _interface->setInvoke(std::function<BaseLib::PVariable(std::string, BaseLib::PArray&)>(std::bind(&RpcServer::invoke, this, std::placeholders::_1, std::placeholders::_2)));

        BaseLib::TcpSocket::TcpServerInfo serverInfo;
        serverInfo.maxConnections = 1;
        serverInfo.useSsl = true;
        serverInfo.certFiles.push_back(GD::settings.certPath());
        serverInfo.keyFiles.push_back(GD::settings.keyPath());
        serverInfo.dhParamFile = GD::settings.dhPath();
        serverInfo.caFiles.push_back(GD::settings.caFile());
        serverInfo.requireClientCert = true;
        serverInfo.newConnectionCallback = std::bind(&RpcServer::newConnection, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        serverInfo.packetReceivedCallback = std::bind(&RpcServer::packetReceived, this, std::placeholders::_1, std::placeholders::_2);

        _tcpServer = std::make_shared<BaseLib::TcpSocket>(_bl, serverInfo);
        std::string boundAddress;
        _tcpServer->startServer(GD::settings.listenAddress(), std::to_string(GD::settings.port()), boundAddress);
        _stopped = false;

        return true;
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
    return false;
}

void RpcServer::stop()
{
    try
    {
        _stopped = true;
        if(_tcpServer)
        {
            _tcpServer->stopServer();
            _tcpServer->waitForServerStopped();
        }
        _interface.reset();
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

void RpcServer::newConnection(int32_t clientId, std::string address, uint16_t port)
{
    try
    {
        GD::out.printInfo("Info: New connection from " + address + " on port " + std::to_string(port) + ".");
        _clientId = clientId;
        _clientConnected = true;
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

void RpcServer::packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet)
{
    try
    {
        _binaryRpc->process((char*)packet.data(), packet.size());
        if(_binaryRpc->isFinished())
        {
            if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request)
            {
                std::string method;
                auto parameters = _rpcDecoder->decodeRequest(_binaryRpc->getData(), method);

                BaseLib::PVariable response = _interface->callMethod(method, parameters);
                std::vector<uint8_t> data;
                _rpcEncoder->encodeResponse(response, data);
                _tcpServer->sendToClient(clientId, data);
            }
            else if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::response && _waitForResponse)
            {
                std::unique_lock<std::mutex> requestLock(_requestMutex);
                _rpcResponse = _rpcDecoder->decodeResponse(_binaryRpc->getData());
                requestLock.unlock();
                _requestConditionVariable.notify_all();
            }
            _binaryRpc->reset();
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

BaseLib::PVariable RpcServer::invoke(std::string methodName, BaseLib::PArray& parameters)
{
    try
    {
        if(_tcpServer->clientCount() == 0) return BaseLib::Variable::createError(-1, "No client connected.");
        std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

        std::unique_lock<std::mutex> requestLock(_requestMutex);
        _rpcResponse.reset();
        _waitForResponse = true;

        std::vector<uint8_t> encodedPacket;
        _rpcEncoder->encodeRequest(methodName, parameters, encodedPacket);

        _tcpServer->sendToClient(_clientId, encodedPacket);

        int32_t i = 0;
        while(!_requestConditionVariable.wait_for(requestLock, std::chrono::milliseconds(1000), [&]
        {
            i++;
            return _rpcResponse || _stopped || i == 10;
        }));
        _waitForResponse = false;
        if(i == 10 || !_rpcResponse) return BaseLib::Variable::createError(-32500, "No RPC response received.");

        return _rpcResponse;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}