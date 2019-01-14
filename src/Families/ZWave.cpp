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

#include "../GD.h"
#include "ZWave.h"


ZWave::ZWave(BaseLib::SharedObjects* bl) : ICommunicationInterface(bl), _stopCallbackThread(false), _stopped(true), _tryCount(30), _emptyReadBuffers(true), lastSOFtime(0)
{
    try
    {
        _familyId = ZWAVE_FAMILY_ID;

        _localRpcMethods.emplace("emptyReadBuffers", std::bind(&ZWave::emptyReadBuffers, this, std::placeholders::_1));
        _localRpcMethods.emplace("sendPacket", std::bind(&ZWave::sendPacket, this, std::placeholders::_1));

        start();
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

ZWave::~ZWave()
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

void ZWave::start()
{
    try
    {
        if(GD::settings.device().empty())
        {
            GD::out.printError("Error: No device defined for family ZWave. Please specify it in \"gateway.conf\".");
            return;
        }

        Reset();
        _serial->openDevice(false, false, false);
        if(!Open()) return;

        _stopCallbackThread = false;

        _bl->threadManager.start(_listenThread, true, &ZWave::listen, this);

        //sendReconnect();
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

void ZWave::stop()
{
    try
    {
        if(!_serial) return;

        _stopCallbackThread = true;
        _bl->threadManager.join(_listenThread);
        SetStopped();
        if(_serial) _serial->closeDevice();
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

uint8_t ZWave::getCrc8(const std::vector<uint8_t>& packet)
{
    uint8_t crc8 = 0xFF;

    for(uint32_t i = 1; i < packet.size() - 1; ++i)
        crc8 ^= packet[i];

    return crc8;
}

void ZWave::rawSend(const std::vector<uint8_t>& packet)
{
    try
    {
        if(!_serial || !_serial->isOpen())
            return;
        GD::out.printInfo("Info: RAW Sending packet " + BaseLib::HelperFunctions::getHexString(packet));
        _serial->writeData(packet);
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
}


void ZWave::sendAck()
{
    try
    {
        std::vector<uint8_t> ack{ (uint8_t)ZWaveResponseCodes::ACK };
        rawSend(ack);
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
}

void ZWave::sendNack()
{
    try
    {
        std::vector<uint8_t> nack{ (uint8_t)ZWaveResponseCodes::NACK };
        rawSend(nack);
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
}

void ZWave::sendCan()
{
    try
    {
        std::vector<uint8_t> can{ (uint8_t)ZWaveResponseCodes::CAN };
        rawSend(can);
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
}


void ZWave::EmptyReadBuffers(int tryCount)
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

void ZWave::listen()
{
    try
    {
        GD::out.printInfo("Listen thread starting");

        std::vector<uint8_t> data;
        data.reserve(200);
        char byte = 0;
        int32_t result = 0;
        uint32_t packetSize = 0;
        uint8_t crc8 = 0;

        //if (IsOpen()) sendReconnect();

        while(!_stopCallbackThread)
        {
            try
            {
                if(!IsOpen())
                {
                    if(_stopCallbackThread)
                    {
                        GD::out.printInfo("Listen thread stopped");
                        SetStopped();
                        return;
                    }
                    if(IsStopped())
                        GD::out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
                    _serial->closeDevice();

                    std::this_thread::sleep_for(std::chrono::seconds(5));

                    if(!_stopCallbackThread)
                        reconnect();
                    else
                    {
                        GD::out.printInfo("Listen thread stopped");
                        SetStopped();
                        return;
                    }

                    continue;
                }
                else if (_emptyReadBuffers)
                {
                    EmptyReadBuffers(_tryCount);
                    _emptyReadBuffers = false;
                    continue;
                }

                byte = 0;
                result = _serial->readChar(byte, 100000);
                if(-1 == result)
                {
                    GD::out.printError("Error reading from serial device.");
                    SetStopped();
                    packetSize = 0;
                    data.clear();
                    continue;
                }
                else if(1 == result)
                {
                    const int64_t curTime = BaseLib::HelperFunctions::getTime();
                    if (curTime - lastSOFtime < 1500) continue;

                    packetSize = 0;

                    if(!data.empty())
                    {
                        GD::out.printWarning("Warning: Incomplete packet received: " + BaseLib::HelperFunctions::getHexString(data));
                        //sendNack();
                        data.clear();

                        data.push_back((uint8_t)ZWaveResponseCodes::NACK);
                        _processRawPacket(data);
                        data.clear();
                    }

                    continue;
                }

                if(data.empty())
                {
                    if(byte == (uint8_t)ZWaveResponseCodes::ACK || byte == (uint8_t)ZWaveResponseCodes::NACK || byte == (uint8_t)ZWaveResponseCodes::CAN)
                    {
                        data.push_back(byte);

                        _processRawPacket(data);

                        data.clear();
                        continue;
                    }
                    else if(byte != (uint8_t)ZWaveResponseCodes::SOF)
                    {
                        GD::out.printWarning("Warning: Unknown start byte received: " + BaseLib::HelperFunctions::getHexString(byte));

                        //sendNack();
                        data.push_back((uint8_t)ZWaveResponseCodes::NACK);
                        _processRawPacket(data);
                        data.clear();

                        continue;
                    }
                    lastSOFtime = BaseLib::HelperFunctions::getTime();
                }
                data.push_back(byte);

                if(0 == packetSize && 2 == data.size())
                {
                    packetSize = data[1];
                    if(0 == packetSize)
                    {
                        GD::out.printError("Error: Header has invalid size information: " + BaseLib::HelperFunctions::getHexString(data));

                        data.clear();

                        //sendNack();

                        data.push_back((uint8_t)ZWaveResponseCodes::NACK);
                        _processRawPacket(data);
                        data.clear();

                        continue;
                    }
                    packetSize += 2;
                }

                if(packetSize > 0 && data.size() == packetSize)
                {
                    crc8 = getCrc8(data);
                    if(crc8 != data.back())
                    {
                        GD::out.printError("Error: CRC failed for packet: " + BaseLib::HelperFunctions::getHexString(data));
                        packetSize = 0;
                        data.clear();
                        sendNack();

                        data.push_back((uint8_t)ZWaveResponseCodes::NACK);
                        _processRawPacket(data);
                        data.clear();

                        continue;
                    }

                    sendAck();

                    packetSize = 0;

                    _processRawPacket(data);

                    data.clear();
                }
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
        }
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

    GD::out.printInfo("Listen thread stopped");
}

void ZWave::_processRawPacket(std::vector<uint8_t> data)
{
    processRawPacket(data);
}


void ZWave::processRawPacket(std::vector<uint8_t>& data)
{
    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(2);
    parameters->push_back(std::make_shared<BaseLib::Variable>(ZWAVE_FAMILY_ID));
    parameters->push_back(std::make_shared<BaseLib::Variable>(data));

    auto result = _invoke("packetReceived", parameters);
    if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
    {
        GD::out.printError("Error calling packetReceived(): " + result->structValue->at("faultString")->stringValue);
    }
}


void ZWave::sendReconnect()
{
    GD::out.printInfo("Calling reconnect on the other end");

    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(1);
    parameters->push_back(std::make_shared<BaseLib::Variable>(ZWAVE_FAMILY_ID));

    auto result = _invoke("reconnect", parameters);
    if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
    {
        GD::out.printError("Error calling reconnect(): " + result->structValue->at("faultString")->stringValue);
    }
}

void ZWave::reconnect()
{
    try
    {
        GD::out.printInfo("Trying to reconnect");

        _serial->closeDevice();
        _stopped = true;
        _serial->openDevice(false, false, false);
        if(!_serial->isOpen())
        {
            GD::out.printError("Error: Could not open device.");
            return;
        }
        _stopped = false;

        sendReconnect();
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

BaseLib::PVariable ZWave::callMethod(std::string& method, BaseLib::PArray parameters)
{
    try
    {
        auto localMethodIterator = _localRpcMethods.find(method);
        if(localMethodIterator == _localRpcMethods.end()) return BaseLib::Variable::createError(-32601, ": Requested method not found.");

        if(_bl->debugLevel >= 5) GD::out.printDebug("Debug: Server is calling RPC method: " + method);

        return localMethodIterator->second(parameters);
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
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}


//{{{ RPC methods
BaseLib::PVariable ZWave::sendPacket(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tBinary || parameters->at(1)->binaryValue.empty()) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_serial)
        {
            GD::out.printError("Error: Couldn't write to device, because the device descriptor is not valid: " + GD::settings.device());
            return BaseLib::Variable::createError(-1, "Serial device is not open.");
        }

        rawSend(parameters->at(1)->binaryValue);

        return std::make_shared<BaseLib::Variable>();
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
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

BaseLib::PVariable ZWave::emptyReadBuffers(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tInteger64 || parameters->at(1)->integerValue64 == 0) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_serial)
        {
            GD::out.printError("Error: Couldn't write to device, because the device descriptor is not valid: " + GD::settings.device());
            return BaseLib::Variable::createError(-1, "Serial device is not open.");
        }

        _tryCount = parameters->at(1)->integerValue64;
        _emptyReadBuffers = true;

        return std::make_shared<BaseLib::Variable>();
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
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

//}}}
