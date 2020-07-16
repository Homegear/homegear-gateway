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

#include "EnOcean.h"
#include "../Gd.h"

EnOcean::EnOcean(BaseLib::SharedObjects* bl) : ICommunicationInterface(bl)
{
    try
    {
        _familyId = ENOCEAN_FAMILY_ID;

        _initComplete = false;
        _stopped = true;

        _localRpcMethods.emplace("sendPacket", std::bind(&EnOcean::sendPacket, this, std::placeholders::_1));
        _localRpcMethods.emplace("getBaseAddress", std::bind(&EnOcean::getBaseAddress, this, std::placeholders::_1));
        _localRpcMethods.emplace("setBaseAddress", std::bind(&EnOcean::setBaseAddress, this, std::placeholders::_1));

        start();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

EnOcean::~EnOcean()
{
    try
    {
        stop();
        Gd::bl->threadManager.join(_initThread);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::start()
{
    try
    {
        if(Gd::settings.device().empty())
        {
            Gd::out.printError("Error: No device defined for family EnOcean. Please specify it in \"gateway.conf\".");
            return;
        }

        _serial.reset(new BaseLib::SerialReaderWriter(_bl, Gd::settings.device(), 57600, 0, true, -1));
        _serial->openDevice(false, false, false);
        if(!_serial->isOpen())
        {
            Gd::out.printError("Error: Could not open device.");
            return;
        }

        _stopped = false;
        _stopCallbackThread = false;
        int32_t result = 0;
        char byte = 0;
        while(result == 0)
        {
            //Clear buffer, otherwise the address response cannot be sent by the module if the buffer is full.
            result = _serial->readChar(byte, 100000);
        }
        _bl->threadManager.start(_listenThread, true, &EnOcean::listen, this);

        init();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::stop()
{
    try
    {
        _stopCallbackThread = true;
        _bl->threadManager.join(_listenThread);
        _initComplete = false;
        _stopped = true;
        if(_serial) _serial->closeDevice();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::getResponse(uint8_t packetType, std::vector<uint8_t>& requestPacket, std::vector<uint8_t>& responsePacket)
{
    try
    {
        if(_stopped) return;
        responsePacket.clear();

        std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
        std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
        std::shared_ptr<Request> request(new Request());
        std::unique_lock<std::mutex> requestsGuard(_requestsMutex);
        _requests[packetType] = request;
        requestsGuard.unlock();
        std::unique_lock<std::mutex> lock(request->mutex);

        try
        {
            Gd::out.printInfo("Info: Sending packet " + BaseLib::HelperFunctions::getHexString(requestPacket));
            rawSend(requestPacket);
        }
        catch(const BaseLib::SocketOperationException& ex)
        {
            Gd::out.printError("Error sending packet: " + std::string(ex.what()));
            return;
        }

        if(!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(10000), [&] { return request->mutexReady; }))
        {
            Gd::out.printError("Error: No response received to packet: " + BaseLib::HelperFunctions::getHexString(requestPacket));
        }
        responsePacket = request->response;

        requestsGuard.lock();
        _requests.erase(packetType);
        requestsGuard.unlock();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::addCrc8(std::vector<uint8_t>& packet)
{
    try
    {
        if(packet.size() < 6) return;

        uint8_t crc8 = 0;
        for(int32_t i = 1; i < 5; i++)
        {
            crc8 = _crc8Table[crc8 ^ (uint8_t)packet[i]];
        }
        packet[5] = crc8;

        crc8 = 0;
        for(uint32_t i = 6; i < packet.size() - 1; i++)
        {
            crc8 = _crc8Table[crc8 ^ (uint8_t)packet[i]];
        }
        packet.back() = crc8;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::reconnect()
{
    try
    {
        _serial->closeDevice();
        _initComplete = false;
        _serial->openDevice(false, false, false);
        if(!_serial->isOpen())
        {
            Gd::out.printError("Error: Could not open device.");
            return;
        }
        _stopped = false;

        Gd::bl->threadManager.join(_initThread);
        _bl->threadManager.start(_initThread, true, &EnOcean::init, this);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::init()
{
    try
    {
        std::vector<uint8_t> response;
        for(int32_t i = 0; i < 10; i++)
        {
            std::vector<uint8_t> data{ 0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00 };
            addCrc8(data);
            getResponse(0x02, data, response);
            if(response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0)
            {
                if(i < 9) continue;
                Gd::out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(data));
                _stopped = true;
                return;
            }
            _baseAddress = ((int32_t)(uint8_t)response[7] << 24) | ((int32_t)(uint8_t)response[8] << 16) | ((int32_t)(uint8_t)response[9] << 8) | (uint8_t)response[10];
            break;
        }

        Gd::out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ". Remaining changes: " + std::to_string(response[11]));

        _initComplete = true;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::listen()
{
    try
    {
        std::vector<uint8_t> data;
        data.reserve(100);
        char byte = 0;
        int32_t result = 0;
        uint32_t size = 0;
        uint8_t crc8 = 0;

        while(!_stopCallbackThread)
        {
            try
            {
                if(_stopped || !_serial || !_serial->isOpen())
                {
                    if(_stopCallbackThread) return;
                    if(_stopped) Gd::out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
                    _serial->closeDevice();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                    reconnect();
                    continue;
                }

                result = _serial->readChar(byte, 100000);
                if(result == -1)
                {
                    Gd::out.printError("Error reading from serial device.");
                    _stopped = true;
                    size = 0;
                    data.clear();
                    continue;
                }
                else if(result == 1)
                {
                    size = 0;
                    data.clear();
                    continue;
                }

                if(data.empty() && byte != 0x55) continue;
                data.push_back((uint8_t)byte);

                if(size == 0 && data.size() == 6)
                {
                    crc8 = 0;
                    for(int32_t i = 1; i < 5; i++)
                    {
                        crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
                    }
                    if(crc8 != data[5])
                    {
                        Gd::out.printError("Error: CRC (0x" + BaseLib::HelperFunctions::getHexString(crc8, 2) + ") failed for header: " + BaseLib::HelperFunctions::getHexString(data));
                        size = 0;
                        data.clear();
                        continue;
                    }
                    size = ((data[1] << 8) | data[2]) + data[3];
                    if(size == 0)
                    {
                        Gd::out.printError("Error: Header has invalid size information: " + BaseLib::HelperFunctions::getHexString(data));
                        size = 0;
                        data.clear();
                        continue;
                    }
                    size += 7;
                }
                if(size > 0 && data.size() == size)
                {
                    crc8 = 0;
                    for(uint32_t i = 6; i < data.size() - 1; i++)
                    {
                        crc8 = _crc8Table[crc8 ^ (uint8_t)data[i]];
                    }
                    if(crc8 != data.back())
                    {
                        Gd::out.printError("Error: CRC failed for packet: " + BaseLib::HelperFunctions::getHexString(data));
                        size = 0;
                        data.clear();
                        continue;
                    }

                    processPacket(data);

                    size = 0;
                    data.clear();
                }
            }
            catch(const std::exception& ex)
            {
                Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::processPacket(std::vector<uint8_t>& data)
{
    try
    {
        if(data.size() < 5)
        {
            Gd::out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
            return;
        }

        if(Gd::bl->debugLevel >= 5) Gd::out.printDebug("Debug: Packet received: " + BaseLib::HelperFunctions::getHexString(data));

        uint8_t packetType = data[4];
        std::unique_lock<std::mutex> requestsGuard(_requestsMutex);
        std::map<uint8_t, std::shared_ptr<Request>>::iterator requestIterator = _requests.find(packetType);
        if(requestIterator != _requests.end())
        {
            std::shared_ptr<Request> request = requestIterator->second;
            requestsGuard.unlock();
            request->response = data;
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->mutexReady = true;
            }
            request->conditionVariable.notify_one();
            return;
        }
        else requestsGuard.unlock();

        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->reserve(2);
        parameters->push_back(std::make_shared<BaseLib::Variable>(ENOCEAN_FAMILY_ID));
        parameters->push_back(std::make_shared<BaseLib::Variable>(data));

        auto result = _invoke("packetReceived", parameters);
        if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
        {
            Gd::out.printError("Error calling packetReceived(): " + result->structValue->at("faultString")->stringValue);
        }
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void EnOcean::rawSend(std::vector<uint8_t>& packet)
{
    try
    {
        if(!_serial || !_serial->isOpen()) return;
        _serial->writeData(packet);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

BaseLib::PVariable EnOcean::callMethod(std::string& method, BaseLib::PArray parameters)
{
    try
    {
        auto localMethodIterator = _localRpcMethods.find(method);
        if(localMethodIterator == _localRpcMethods.end()) return BaseLib::Variable::createError(-32601, ": Requested method not found.");

        if(Gd::bl->debugLevel >= 5) Gd::out.printDebug("Debug: Server is calling RPC method: " + method);

        return localMethodIterator->second(parameters);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

//{{{ RPC methods
BaseLib::PVariable EnOcean::sendPacket(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tBinary || parameters->at(1)->binaryValue.empty()) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_initComplete)
        {
            Gd::out.printInfo("Info: Waiting one second, because init is not complete.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if(!_initComplete)
            {
                Gd::out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(parameters->at(1)->binaryValue) + ", because init is not complete.");
                return BaseLib::Variable::createError(-2, "Could not send packet. Init is not complete.");
            }
        }

        rawSend(parameters->at(1)->binaryValue);
        return std::make_shared<BaseLib::Variable>();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

BaseLib::PVariable EnOcean::getBaseAddress(BaseLib::PArray& parameters)
{
    try
    {
        if(!_initComplete)
        {
            Gd::out.printInfo("Info: Waiting one second, because init is not complete.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if(!_initComplete)
            {
                Gd::out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(parameters->at(1)->binaryValue) + ", because init is not complete.");
                return BaseLib::Variable::createError(-2, "Could not get base address. Init is not complete.");
            }
        }

        return std::make_shared<BaseLib::Variable>(_baseAddress);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

BaseLib::PVariable EnOcean::setBaseAddress(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tInteger64) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_initComplete)
        {
            Gd::out.printInfo("Info: Waiting one second, because init is not complete.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if(!_initComplete)
            {
                Gd::out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(parameters->at(1)->binaryValue) + ", because init is not complete.");
                return BaseLib::Variable::createError(-2, "Could not set base address. Init is not complete.");
            }
        }

        uint32_t value = (uint32_t)(int32_t)parameters->at(1)->integerValue64;

        if((value & 0xFF000000) != 0xFF000000)
        {
            Gd::out.printError("Error: Could not set base address. Address must start with 0xFF.");
            return BaseLib::Variable::createError(-3, "Could not set base address. Address must start with 0xFF.");
        }

        std::vector<uint8_t> response;

        {
            // Set address - only possible 10 times, Must start with "0xFF"
            std::vector<uint8_t> data{ 0x55, 0x00, 0x05, 0x00, 0x05, 0x00, 0x07, (uint8_t)(value >> 24), (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF), 0x00 };
            addCrc8(data);
            getResponse(0x02, data, response);
            if(response.size() != 8 || response[1] != 0 || response[2] != 1 || response[3] != 0 || response[4] != 2 || response[6] != 0)
            {
                Gd::out.printError("Error setting address on device: " + BaseLib::HelperFunctions::getHexString(data));
                _stopped = true;
                return BaseLib::Variable::createError(-4, "Error setting address on device: " + BaseLib::HelperFunctions::getHexString(data));
            }
        }

        for(int32_t i = 0; i < 10; i++)
        {
            std::vector<uint8_t> data{ 0x55, 0x00, 0x01, 0x00, 0x05, 0x00, 0x08, 0x00 };
            addCrc8(data);
            getResponse(0x02, data, response);
            if(response.size() != 13 || response[1] != 0 || response[2] != 5 || response[3] != 1 || response[6] != 0)
            {
                if(i < 9) continue;
                Gd::out.printError("Error reading address from device: " + BaseLib::HelperFunctions::getHexString(data));
                _stopped = true;
                return BaseLib::Variable::createError(-5, "Error reading address from device: " + BaseLib::HelperFunctions::getHexString(data));
            }
            _baseAddress = ((int32_t)(uint8_t)response[7] << 24) | ((int32_t)(uint8_t)response[8] << 16) | ((int32_t)(uint8_t)response[9] << 8) | (uint8_t)response[10];
            break;
        }

        Gd::out.printInfo("Info: Base address set to 0x" + BaseLib::HelperFunctions::getHexString(_baseAddress, 8) + ". Remaining changes: " + std::to_string(response[11]));

        return std::make_shared<BaseLib::Variable>((int32_t)response[11]);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}
//}}}