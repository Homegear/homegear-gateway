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

#include "RpcServer.h"
#include "Gd.h"
#include "Families/EnOcean.h"
#include "Families/HomeMaticCulfw.h"
#include "Families/MaxCulfw.h"
#ifdef SPISUPPORT
#include "Families/Cc110LTest.h"
#include "Families/HomeMaticCc1101.h"
#include "Families/MaxCc1101.h"
#endif
#include "Families/ZWave.h"

RpcServer::RpcServer(BaseLib::SharedObjects* bl)
{
    signal(SIGPIPE, SIG_IGN);

    _stopped = true;
    _unconfigured = false;
    _clientConnected = false;
    _waitForResponse = false;

	_bl = bl;
    _binaryRpc.reset(new BaseLib::Rpc::BinaryRpc(bl));
    _rpcDecoder.reset(new BaseLib::Rpc::RpcDecoder(bl, false, false));
    _rpcEncoder.reset(new BaseLib::Rpc::RpcEncoder(bl, true, true));
}

RpcServer::~RpcServer()
{
    std::lock_guard<std::mutex> maintenanceThreadGuard(_maintenanceThreadMutex);
    _bl->threadManager.join(_maintenanceThread);
}

int32_t RpcServer::familyId()
{
    if(_interface) return _interface->familyId();

    return -1;
}

bool RpcServer::start()
{
    try
    {
        _unconfigured = false;

        if(Gd::settings.family().empty())
        {
            Gd::out.printError("Error: Setting family in gateway.conf is empty.");
            return false;
        }

        if(Gd::settings.family() == "enocean") _interface = std::unique_ptr<EnOcean>(new EnOcean(_bl));
        else if(Gd::settings.family() == "homematicculfw") _interface = std::unique_ptr<HomeMaticCulfw>(new HomeMaticCulfw(_bl));
        else if(Gd::settings.family() == "maxculfw") _interface = std::unique_ptr<HomeMaticCulfw>(new HomeMaticCulfw(_bl));
        else if(Gd::settings.family() == "zwave") _interface = std::unique_ptr<ZWave>(new ZWave(_bl));
#ifdef SPISUPPORT
        else if(GD::settings.family() == "cc110ltest") _interface = std::unique_ptr<Cc110LTest>(new Cc110LTest(_bl));
        else if(GD::settings.family() == "homematiccc1101") _interface = std::unique_ptr<HomeMaticCc1101>(new HomeMaticCc1101(_bl));
        else if(GD::settings.family() == "maxcc1101") _interface = std::unique_ptr<HomeMaticCc1101>(new HomeMaticCc1101(_bl));
#endif

        if(!_interface)
        {
            Gd::out.printError("Error: Unknown family: " + Gd::settings.family() + ". Please correct it in gateway.conf.");
            return false;
        }

        _interface->setInvoke(std::function<BaseLib::PVariable(std::string, BaseLib::PArray&)>(std::bind(&RpcServer::invoke, this, std::placeholders::_1, std::placeholders::_2)));

        BaseLib::TcpSocket::TcpServerInfo serverInfo;
        serverInfo.maxConnections = 1;
        serverInfo.useSsl = true;
        BaseLib::TcpSocket::PCertificateInfo certificateInfo = std::make_shared<BaseLib::TcpSocket::CertificateInfo>();

        std::string caFile = Gd::settings.caFile();
        if(caFile.empty()) caFile = Gd::settings.dataPath() + "ca.crt";
        if(!BaseLib::Io::fileExists(caFile))
        {
            caFile = "";
            _unconfigured = true;
            serverInfo.useSsl = false;
        }
        certificateInfo->caFile = caFile;

        std::string certFile = Gd::settings.certPath();
        if(certFile.empty()) certFile = Gd::settings.dataPath() + "gateway.crt";
        if(!BaseLib::Io::fileExists(certFile))
        {
            certFile = "";
            _unconfigured = true;
            serverInfo.useSsl = false;
        }
        certificateInfo->certFile = certFile;

        std::string keyFile = Gd::settings.keyPath();
        if(keyFile.empty()) keyFile = Gd::settings.dataPath() + "gateway.key";
        if(!BaseLib::Io::fileExists(keyFile))
        {
            keyFile = "";
            _unconfigured = true;
            serverInfo.useSsl = false;
        }
        certificateInfo->keyFile = keyFile;

        if(_unconfigured && Gd::settings.configurationPassword().empty())
        {
            _interface.reset();
            Gd::out.printError("Error: Gateway is unconfigured but configurationPassword is not set in gateway.conf.");
            return false;
        }

        if(!_unconfigured)
        {
            serverInfo.certificates.emplace("*", certificateInfo);
            std::string dhFile = Gd::settings.dhPath();
            if(dhFile.empty()) dhFile = Gd::settings.dataPath() + "dh.pem";
            serverInfo.dhParamFile = dhFile;
            serverInfo.requireClientCert = true;
        }
        else Gd::out.printWarning("Warning: Gateway is not fully configured yet.");
        serverInfo.newConnectionCallback = std::bind(&RpcServer::newConnection, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        serverInfo.packetReceivedCallback = std::bind(&RpcServer::packetReceived, this, std::placeholders::_1, std::placeholders::_2);

        _tcpServer = std::make_shared<BaseLib::TcpSocket>(_bl, serverInfo);
        std::string boundAddress;
        _tcpServer->startServer(Gd::settings.listenAddress(), std::to_string(_unconfigured ? Gd::settings.portUnconfigured() : Gd::settings.port()), boundAddress);
        _stopped = false;

        return true;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
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
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void RpcServer::restart()
{
    Gd::out.printMessage("Restarting server.");

    Gd::upnp->stop();

    stop();
    start();

    Gd::upnp->start();
}

BaseLib::PVariable RpcServer::configure(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 1) return BaseLib::Variable::createError(-1, "Wrong parameter count.");
        if(parameters->at(0)->type != BaseLib::VariableType::tString) return BaseLib::Variable::createError(-1, "Parameter is not of type String.");
        if(parameters->at(0)->stringValue.size() < 128 || parameters->at(0)->stringValue.size() > 100000) return BaseLib::Variable::createError(-2, "Data is invalid.");

        BaseLib::Security::Gcrypt aes(GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_GCM, GCRY_CIPHER_SECURE);

        std::vector<uint8_t> key;
        if(!BaseLib::Security::Hash::sha256(Gd::bl->hf.getUBinary(Gd::settings.configurationPassword()), key) || key.empty())
        {
            Gd::out.printError("Error: Could not generate SHA256 of configuration password.");
            return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
        }
        aes.setKey(key);

        std::vector<uint8_t> iv = _bl->hf.getUBinary(parameters->at(0)->stringValue.substr(0, 24));
        aes.setIv(iv);

        std::vector<uint8_t> counter(16);
        aes.setCounter(counter);

        std::vector<uint8_t> payload = _bl->hf.getUBinary(parameters->at(0)->stringValue.substr(24));
        if(!aes.authenticate(payload)) return BaseLib::Variable::createError(-2, "Data is invalid.");

        std::vector<uint8_t> decryptedData;
        aes.decrypt(decryptedData, payload);

        BaseLib::Rpc::RpcDecoder rpcDecoder(_bl, false, false);
        auto data = rpcDecoder.decodeResponse(decryptedData);

        if(data->type != BaseLib::VariableType::tStruct) return BaseLib::Variable::createError(-1, "Data is not of type Struct.");

        auto dataIterator = data->structValue->find("caCert");
        if(dataIterator == data->structValue->end()) return BaseLib::Variable::createError(-1, "Data does not contain element \"caCert\".");
        std::string certPath = Gd::settings.dataPath() + "ca.crt";
        BaseLib::Io::writeFile(certPath, dataIterator->second->stringValue);

        dataIterator = data->structValue->find("gatewayCert");
        if(dataIterator == data->structValue->end()) return BaseLib::Variable::createError(-1, "Data does not contain element \"gatewayCert\".");
        certPath = Gd::settings.dataPath() + "gateway.crt";
        BaseLib::Io::writeFile(certPath, dataIterator->second->stringValue);

        dataIterator = data->structValue->find("gatewayKey");
        if(dataIterator == data->structValue->end()) return BaseLib::Variable::createError(-1, "Data does not contain element \"gatewayKey\".");
        certPath = Gd::settings.dataPath() + "gateway.key";
        BaseLib::Io::writeFile(certPath, dataIterator->second->stringValue);

        uid_t userId = Gd::bl->hf.userId(Gd::runAsUser);
        gid_t groupId = Gd::bl->hf.groupId(Gd::runAsGroup);

        if(chown(certPath.c_str(), userId, groupId) == -1) Gd::out.printWarning("Warning: Could net set owner on " + certPath + ": " + std::string(strerror(errno)));
        if(chmod(certPath.c_str(), S_IRUSR | S_IWUSR) == -1) Gd::out.printWarning("Warning: Could net set permissions on " + certPath + ": " + std::string(strerror(errno)));;

        Gd::out.printMessage("Remote configuration was successful.");

        return std::make_shared<BaseLib::Variable>();
    }
    catch(BaseLib::Security::GcryptException& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, "Error decrypting data: " + std::string(ex.what()));
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void RpcServer::newConnection(int32_t clientId, std::string address, uint16_t port)
{
    try
    {
        Gd::out.printInfo("Info: New connection from " + address + " on port " + std::to_string(port) + ".");
        _clientId = clientId;
        _clientConnected = true;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void RpcServer::packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet)
{
    try
    {
        int32_t processedBytes = 0;
        while(processedBytes < (signed)packet.size())
        {
            processedBytes += _binaryRpc->process((char*) packet.data() + processedBytes, packet.size() - processedBytes);
            if(_binaryRpc->isFinished())
            {
                if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request)
                {
                    std::string method;
                    auto parameters = _rpcDecoder->decodeRequest(_binaryRpc->getData(), method);

                    BaseLib::PVariable response;
                    if(_unconfigured)
                    {
                        if(method == "configure")
                        {
                            response = configure(parameters);

                            if(!response->errorStruct) Gd::upnp->stop();

                            std::vector<uint8_t> data;
                            _rpcEncoder->encodeResponse(response, data);
                            _tcpServer->sendToClient(clientId, data, true);

                            if(!response->errorStruct)
                            {
                                std::lock_guard<std::mutex> maintenanceThreadGuard(_maintenanceThreadMutex);
                                _bl->threadManager.join(_maintenanceThread);
                                _bl->threadManager.start(_maintenanceThread, true, &RpcServer::restart, this);
                            }
                        }
                        else
                        {
                            response = BaseLib::Variable::createError(-1, "Unknown method.");
                            std::vector<uint8_t> data;
                            _rpcEncoder->encodeResponse(response, data);
                            _tcpServer->sendToClient(clientId, data, true);
                        }
                    }
                    else
                    {
                        response = _interface->callMethod(method, parameters);
                        std::vector<uint8_t> data;
                        _rpcEncoder->encodeResponse(response, data);
                        _tcpServer->sendToClient(clientId, data);
                    }
                }
                else if(!_unconfigured && _binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::response && _waitForResponse)
                {
                    std::unique_lock<std::mutex> requestLock(_requestMutex);
                    _rpcResponse = _rpcDecoder->decodeResponse(_binaryRpc->getData());
                    requestLock.unlock();
                    _requestConditionVariable.notify_all();
                }
                _binaryRpc->reset();
            }
        }
    }
    catch(BaseLib::Rpc::BinaryRpcException& ex)
    {
        _binaryRpc->reset();
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, "Error processing packet: " + std::string(ex.what()));
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

BaseLib::PVariable RpcServer::invoke(std::string methodName, BaseLib::PArray& parameters)
{
    try
    {
        if(_unconfigured || _tcpServer->clientCount() == 0) return BaseLib::Variable::createError(-1, "No client connected.");
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
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void RpcServer::txTest()
{
    try
    {
        std::string method = "txTest";
        auto parameters = std::make_shared<BaseLib::Array>();
        _interface->callMethod(method, parameters);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}