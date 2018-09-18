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

#ifndef HOMEGEAR_GATEWAY_ZWAVE_H
#define HOMEGEAR_GATEWAY_ZWAVE_H

#include "ICommunicationInterface.h"


#define ZWAVE_FAMILY_ID 17

class ZWave : public ICommunicationInterface
{
public:
    ZWave(BaseLib::SharedObjects* bl);
    virtual ~ZWave();
    virtual BaseLib::PVariable callMethod(std::string& method, BaseLib::PArray parameters);
private:

    bool IsOpen() const
    {
        return _serial && _serial->isOpen() && !IsStopped();
    }

    bool Open()
    {
        _serial->openDevice(false, false, false);
        if(!_serial->isOpen())
        {
            GD::out.printError("Error: Could not open device.");
            SetStopped(); // to be sure
            return false;
        }
        SetStopped(false);

        return true;
    }

    void Reset()
    {
        _serial.reset(new BaseLib::SerialReaderWriter(_bl, GD::settings.device(), /*57600*/115200, 0, true, -1));
    }

    void Close()
    {
        if (_serial) _serial->closeDevice();
        SetStopped();
    }

    bool IsStopped() const
    {
        return _stopped;
    }

    void SetStopped(bool stop = true)
    {
        _stopped = stop;
    }

    enum class ZWaveResponseCodes : uint8_t
    {
        SOF = 0x01, // start of frame
        ACK = 0x06, // acknowledge
        NACK = 0x15, // not acknowledge
        CAN = 0x18 // cancel, send again
    };

    std::atomic_bool _stopCallbackThread;
    std::thread _listenThread;

    std::unique_ptr<BaseLib::SerialReaderWriter> _serial;

    std::atomic_bool _stopped;
    std::atomic_int _tryCount;
    std::atomic_bool _emptyReadBuffers;

    void start();
    void stop();
    void reconnect();
    void sendReconnect();

    void EmptyReadBuffers(int tryCount = 30);
    void rawSend(const std::vector<uint8_t>& packet);
    void listen();
    void sendAck();
    void sendNack();
    void sendCan();

    void processRawPacket(std::vector<uint8_t>& data);
    void _processRawPacket(std::vector<uint8_t> data);

    static uint8_t getCrc8(const std::vector<uint8_t>& packet);

    static bool IsSecurityEncapsulation(uint8_t codecls, uint8_t codecmd)
    {
        return 0x98 == codecls && (0x81 == codecmd || 0xC1 == codecmd);
    }

    static uint8_t function(const std::vector<uint8_t>& data)
    {
        return data.size() > 3 ? data.at(3) : 0;
    }

    static bool CmdFunction(const std::vector<uint8_t>& data)
    {
        uint8_t func = function(data);

        return func == 0x13 || func == 0x04 || func == 0xA8;
    }

    static unsigned int CommandIndex(const std::vector<uint8_t>& data)
    {
        unsigned int pos = 6;
        if (0xA8 == function(data)) pos = 8;
        else if (0x04  == function(data)) pos = 7;

        return pos;
    }

    static uint8_t GetNodeID(const std::vector<uint8_t>& data)
    {
        const uint8_t funcId = function(data);

        if (0xA8 == funcId && data.size() > 6) return data.at(6);
        else if ((0x04  == funcId || 0x49 == funcId) && data.size() > 5) return data.at(5);
        else if (data.size() > 4) return data.at(4);

        return 0;
    }



//{{{ RPC methods
    BaseLib::PVariable sendPacket(BaseLib::PArray& parameters);
    BaseLib::PVariable emptyReadBuffers(BaseLib::PArray& parameters);
//}}}
};

#endif

