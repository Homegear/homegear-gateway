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

#ifndef HOMEGEAR_GATEWAY_ZIGBEE_H
#define HOMEGEAR_GATEWAY_ZIGBEE_H

#include "ICommunicationInterface.h"


#define ZIGBEE_FAMILY_ID 26

class Zigbee : public ICommunicationInterface
{
public:
    Zigbee(BaseLib::SharedObjects* bl);
    virtual ~Zigbee();
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
            Gd::out.printError("Error: Could not open device.");
            SetStopped(); // to be sure
            return false;
        }
        SetStopped(false);

        return true;
    }

    void Reset()
    {
        _serial.reset(new BaseLib::SerialReaderWriter(_bl, Gd::settings.device(), 115200, 0, true, -1));
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

    std::atomic_bool _stopCallbackThread;
    std::thread _listenThread;

    std::unique_ptr<BaseLib::SerialReaderWriter> _serial;

    std::atomic_bool _stopped;
    std::atomic_int _tryCount;

    std::atomic_bool _emptyReadBuffers;
    std::mutex _mutex;
    std::condition_variable _cv;

    int64_t lastSOFtime;

    void start();
    void stop();
    void reconnect();
    void sendReconnect();

    void EmptyReadBuffers(int tryCount = 30);
    void rawSend(const std::vector<uint8_t>& packet);
    void listen();

    void processRawPacket(std::vector<uint8_t>& data);
    void _processRawPacket(std::vector<uint8_t> data);

    static uint8_t getCrc8(const std::vector<uint8_t>& packet);

//{{{ RPC methods
    BaseLib::PVariable sendPacket(BaseLib::PArray& parameters);
    BaseLib::PVariable emptyReadBuffers(BaseLib::PArray& parameters);
//}}}
};

#endif

