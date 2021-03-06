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

#include "../Gd.h"
#include "Zigbee.h"


Zigbee::Zigbee(BaseLib::SharedObjects* bl) : ICommunicationInterface(bl), _stopCallbackThread(false), _stopped(true), _tryCount(30), _emptyReadBuffers(true), lastSOFtime(0)
{
    try
    {
        _familyId = ZIGBEE_FAMILY_ID;

        _localRpcMethods.emplace("emptyReadBuffers", std::bind(&Zigbee::emptyReadBuffers, this, std::placeholders::_1));
        _localRpcMethods.emplace("sendPacket", std::bind(&Zigbee::sendPacket, this, std::placeholders::_1));

        start();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

Zigbee::~Zigbee()
{
    try
    {
        stop();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Zigbee::start()
{
    try
    {
        if(Gd::settings.device().empty())
        {
            Gd::out.printError("Error: No device defined for family Zigbee. Please specify it in \"gateway.conf\".");
            return;
        }

        Reset();
        if(!Open()) return;

        _stopCallbackThread = false;

        _bl->threadManager.start(_listenThread, true, &Zigbee::listen, this);

        //sendReconnect();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Zigbee::stop()
{
    try
    {
        if(!_serial) return;

        _stopCallbackThread = true;
        _bl->threadManager.join(_listenThread);
        SetStopped();
        if(_serial) _serial->closeDevice();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

uint8_t Zigbee::getCrc8(const std::vector<uint8_t>& packet)
{
    uint8_t crc8 = 0x0;

    for(uint32_t i = 1; i < packet.size() - 1; ++i)
        crc8 ^= packet[i];

    return crc8;
}

void Zigbee::rawSend(const std::vector<uint8_t>& packet)
{
    try
    {
        if(!_serial || !_serial->isOpen())
            return;
        Gd::out.printInfo("Info: RAW Sending packet " + BaseLib::HelperFunctions::getHexString(packet));
        _serial->writeData(packet);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}



void Zigbee::EmptyReadBuffers(int tryCount)
{
    char byte = 0;
    int cnt = 0;
    int32_t result = 0;

    if (_stopCallbackThread)
        return;

    do
    {
        //Clear buffer, otherwise the address response cannot be sent by the module if the buffer is full.
        result = _serial->readChar(byte, 100000);
        ++cnt;
    }
    while(0 == result && cnt < tryCount && !_stopCallbackThread);
}

void Zigbee::listen()
{
    try
    {
        Gd::out.printInfo("Listen thread starting");

        std::vector<uint8_t> data;
        data.reserve(255);
        char byte = 0;
        int32_t result = 0;
        uint32_t packetSize = 0;
        uint8_t crc8 = 0;
        int errorReadCount = 0;

        //if (IsOpen()) sendReconnect();

        while(!_stopCallbackThread)
        {
            try
            {
                if(!IsOpen())
                {
                    if(_stopCallbackThread)
                    {
                        Gd::out.printInfo("Listen thread stopped");
                        SetStopped();
                        return;
                    }
                    if(IsStopped())
                        Gd::out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
                    _serial->closeDevice();

                    std::this_thread::sleep_for(std::chrono::seconds(5));

                    if(!_stopCallbackThread)
                        reconnect();
                    else
                    {
                        Gd::out.printInfo("Listen thread stopped");
                        SetStopped();
                        return;
                    }

                    continue;
                }
                else if (_emptyReadBuffers)
                {
                    EmptyReadBuffers(_tryCount);

                    std::unique_lock<std::mutex> lock(_mutex);
                    _emptyReadBuffers = false;
                    lock.unlock();
                    _cv.notify_all();

                    continue;
                }

                byte = 0;
                result = _serial->readChar(byte, 100000);
                if(-1 == result)
                {
                    Gd::out.printError("Error reading from serial device.");

                    if (++errorReadCount > 5)
                    {
                        Gd::out.printError("Couldn't recover from errors reading from serial device, closing it for reopen...");

                        SetStopped();
                        errorReadCount = 0;
                        packetSize = 0;
                        data.clear();
                    }
                    else
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    continue;
                }
                else if(1 == result)
                {
                    const int64_t curTime = BaseLib::HelperFunctions::getTime();
                    if (curTime - lastSOFtime < 1500) continue;

                    packetSize = 0;

                    if(!data.empty())
                    {
                        Gd::out.printWarning("Warning: Incomplete packet received: " + BaseLib::HelperFunctions::getHexString(data));
                        data.clear();
                    }

                    continue;
                }

                errorReadCount = 0;

                if(data.empty())
                {
                    if(static_cast<uint8_t>(byte) != 0xFE)
                    {
                        Gd::out.printWarning("Warning: Unknown start byte received: " + BaseLib::HelperFunctions::getHexString(byte));

                        data.clear();

                        continue;
                    }
                    lastSOFtime = BaseLib::HelperFunctions::getTime();
                }
                data.push_back(byte);

                if(0 == packetSize && 2 == data.size())
                {
                    packetSize = data[1];
                    packetSize += 5;
                }

                if(packetSize > 0 && data.size() == packetSize)
                {
                    crc8 = getCrc8(data);
                    if(crc8 != data.back())
                    {
                        Gd::out.printError("Error: CRC failed for packet: " + BaseLib::HelperFunctions::getHexString(data));
                        packetSize = 0;
                        data.clear();

                        continue;
                    }

                    packetSize = 0;

                    _processRawPacket(data);

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

    Gd::out.printInfo("Listen thread stopped");
}

void Zigbee::_processRawPacket(std::vector<uint8_t> data)
{
    processRawPacket(data);
}


void Zigbee::processRawPacket(std::vector<uint8_t>& data)
{
    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(2);
    parameters->push_back(std::make_shared<BaseLib::Variable>(ZIGBEE_FAMILY_ID));
    parameters->push_back(std::make_shared<BaseLib::Variable>(data));

    auto result = _invoke("packetReceived", parameters);
    if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
    {
        Gd::out.printError("Error calling packetReceived(): " + result->structValue->at("faultString")->stringValue);
    }
}


void Zigbee::sendReconnect()
{
    Gd::out.printInfo("Calling reconnect on the other end");

    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(1);
    parameters->push_back(std::make_shared<BaseLib::Variable>(ZIGBEE_FAMILY_ID));

    auto result = _invoke("reconnect", parameters);
    if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
    {
        Gd::out.printError("Error calling reconnect(): " + result->structValue->at("faultString")->stringValue);
    }
}

void Zigbee::reconnect()
{
    try
    {
        Gd::out.printInfo("Trying to reconnect");

        _serial->closeDevice();
        _stopped = true;
        _serial->openDevice(false, false, false);
        if(!_serial->isOpen())
        {
            Gd::out.printError("Error: Could not open device.");
            return;
        }
        _stopped = false;

        sendReconnect();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

BaseLib::PVariable Zigbee::callMethod(std::string& method, BaseLib::PArray parameters)
{
    try
    {
        auto localMethodIterator = _localRpcMethods.find(method);
        if(localMethodIterator == _localRpcMethods.end()) return BaseLib::Variable::createError(-32601, ": Requested method not found.");

        if(_bl->debugLevel >= 5) Gd::out.printDebug("Debug: Server is calling RPC method: " + method);

        return localMethodIterator->second(parameters);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}


//{{{ RPC methods
BaseLib::PVariable Zigbee::sendPacket(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tBinary || parameters->at(1)->binaryValue.empty()) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_serial)
        {
            Gd::out.printError("Error: Couldn't write to device, because the device descriptor is not valid: " + Gd::settings.device());
            return BaseLib::Variable::createError(-1, "Serial device is not open.");
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

BaseLib::PVariable Zigbee::emptyReadBuffers(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tInteger64 || parameters->at(1)->integerValue64 == 0) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_serial)
        {
            Gd::out.printError("Error: Couldn't write to device, because the device descriptor is not valid: " + Gd::settings.device());
            return BaseLib::Variable::createError(-1, "Serial device is not open.");
        }

        _tryCount = parameters->at(1)->integerValue64;

        std::unique_lock<std::mutex> lock(_mutex);
        _emptyReadBuffers = true;
        _cv.wait_for(lock, std::chrono::milliseconds(_tryCount * 200), [&] { return !_emptyReadBuffers; });

        return std::make_shared<BaseLib::Variable>();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

//}}}
