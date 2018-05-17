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

#include "HomeMaticCulfw.h"
#include "../GD.h"

HomeMaticCulfw::HomeMaticCulfw(BaseLib::SharedObjects* bl) : ICommunicationInterface(bl)
{
    try
    {
        _familyId = HOMEMATIC_COC_FAMILY_ID;

        _updateMode = false;

        _localRpcMethods.emplace("sendPacket", std::bind(&HomeMaticCulfw::sendPacket, this, std::placeholders::_1));
        _localRpcMethods.emplace("enableUpdateMode", std::bind(&HomeMaticCulfw::enableUpdateMode, this, std::placeholders::_1));
        _localRpcMethods.emplace("disableUpdateMode", std::bind(&HomeMaticCulfw::disableUpdateMode, this, std::placeholders::_1));

        _gpio.reset(new BaseLib::LowLevel::Gpio(bl));

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

HomeMaticCulfw::~HomeMaticCulfw()
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

void HomeMaticCulfw::start()
{
    try
    {
        if(GD::settings.device().empty())
        {
            GD::out.printError("Error: No device defined for family HomeMatic BidCoS CUL. Please specify it in \"gateway.conf\".");
            return;
        }

        _serial.reset(new BaseLib::SerialReaderWriter(_bl, GD::settings.device(), 38400, 0, true, 45));
        _eventHandlerSelf = _serial->addEventHandler(this);
        _serial->openDevice(false, false, true);
        if(!_serial->isOpen())
        {
            GD::out.printError("Error: Could not open device.");
            return;
        }

        if(GD::settings.gpio2() != -1)
        {
            _gpio->openDevice(GD::settings.gpio2(), false);
            if(!_gpio->get(GD::settings.gpio2())) _gpio->set(GD::settings.gpio2(), true);
            _gpio->closeDevice(GD::settings.gpio2());
        }
        if(GD::settings.gpio1() != -1)
        {
            _gpio->openDevice(GD::settings.gpio1(), false);
            _gpio->set(GD::settings.gpio1(), false);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            _gpio->set(GD::settings.gpio1(), true);
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            _gpio->closeDevice(GD::settings.gpio1());
        }

        std::string packet = "X21\nAr\n";
        _serial->writeLine(packet);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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

void HomeMaticCulfw::stop()
{
    try
    {
        if(!_serial) return;
        _serial->removeEventHandler(_eventHandlerSelf);
        _serial->closeDevice();
        _serial.reset();
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

void HomeMaticCulfw::lineReceived(const std::string& data)
{
    try
    {
        if(data.size() > 21) //21 is minimal packet length (=10 Byte + COC "A" + "\n")
        {
            std::string packetHex = data.substr(1);
            BaseLib::HelperFunctions::trim(packetHex);
            BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
            parameters->reserve(2);
            parameters->push_back(std::make_shared<BaseLib::Variable>(HOMEMATIC_COC_FAMILY_ID));
            parameters->push_back(std::make_shared<BaseLib::Variable>(data));

            if(_invoke)
            {
                auto result = _invoke("packetReceived", parameters);
                if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
                {
                    GD::out.printError("Error calling packetReceived(): " + result->structValue->at("faultString")->stringValue);
                }
            }
        }
        else if(!data.empty())
        {
            if(data.compare(0, 4, "LOVF") == 0) GD::out.printWarning("Warning: COC with reached 1% limit. You need to wait, before sending is allowed again.");
            else if(data == "A") return;
            else GD::out.printWarning("Warning: Too short packet received: " + data);
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

BaseLib::PVariable HomeMaticCulfw::callMethod(std::string& method, BaseLib::PArray parameters)
{
    try
    {
        auto localMethodIterator = _localRpcMethods.find(method);
        if(localMethodIterator == _localRpcMethods.end()) return BaseLib::Variable::createError(-32601, ": Requested method not found.");

        if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Server is calling RPC method: " + method);

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
BaseLib::PVariable HomeMaticCulfw::sendPacket(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 2 || parameters->at(1)->type != BaseLib::VariableType::tString || parameters->at(1)->stringValue.empty()) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_serial)
        {
            GD::out.printError("Error: Couldn't write to device, because the device descriptor is not valid: " + GD::settings.device());
            return BaseLib::Variable::createError(-1, "Serial device is not open.");
        }

        std::string packet = "As" + parameters->at(1)->stringValue + "\n" + (_updateMode ? "" : "Ar\n");
        _serial->writeLine(packet);
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

BaseLib::PVariable HomeMaticCulfw::enableUpdateMode(BaseLib::PArray& parameters)
{
    try
    {
        _updateMode = true;
        std::string packet = "AR\n";
        _serial->writeLine(packet);

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

BaseLib::PVariable HomeMaticCulfw::disableUpdateMode(BaseLib::PArray& parameters)
{
    try
    {
        _updateMode = false;
        std::string packet = "Ar\n";
        _serial->writeLine(packet);

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