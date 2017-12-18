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

#ifndef HOMEGEAR_GATEWAY_HOMEMATIC_COC_H
#define HOMEGEAR_GATEWAY_HOMEMATIC_COC_H

#include "ICommunicationInterface.h"

#define HOMEMATIC_COC_FAMILY_ID 0

class HomeMaticCulfw : public ICommunicationInterface, public BaseLib::SerialReaderWriter::ISerialReaderWriterEventSink
{
public:
    HomeMaticCulfw(BaseLib::SharedObjects* bl);
    virtual ~HomeMaticCulfw();
    virtual BaseLib::PVariable callMethod(std::string& method, BaseLib::PArray parameters);
private:
    std::atomic_bool _updateMode;

    std::unique_ptr<BaseLib::LowLevel::Gpio> _gpio;
    std::unique_ptr<BaseLib::SerialReaderWriter> _serial;

    void start();
    void stop();

// {{{ Event handling
    BaseLib::PEventHandler _eventHandlerSelf;
    virtual void lineReceived(const std::string& data);
// }}}

//{{{ RPC methods
    BaseLib::PVariable sendPacket(BaseLib::PArray& parameters);
    BaseLib::PVariable enableUpdateMode(BaseLib::PArray& parameters);
    BaseLib::PVariable disableUpdateMode(BaseLib::PArray& parameters);
//}}}
};

#endif
