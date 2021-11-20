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

#include "MaxCc1101.h"
#ifdef SPISUPPORT
#include "../Gd.h"

MaxCc1101::MaxCc1101(BaseLib::SharedObjects* bl) : ICommunicationInterface(bl)
{
    try
    {
        _familyId = MAX_CC1101_FAMILY_ID;

        _stopCallbackThread = true;
        _stopped = true;
        _sending = false;
        _sendingPending = false;
        _firstPacket = true;
        _updateMode = false;
        _gpio.reset(new BaseLib::LowLevel::Gpio(bl, Gd::settings.gpioPath()));

        _localRpcMethods.emplace("sendPacket", std::bind(&MaxCc1101::sendPacket, this, std::placeholders::_1));

        _oscillatorFrequency = Gd::settings.oscillatorFrequency();
        _interruptPin = Gd::settings.interruptPin();

        if(_oscillatorFrequency < 0) _oscillatorFrequency = 26000000;
        if(_interruptPin != 0 && _interruptPin != 2)
        {
            if(_interruptPin > 0) Gd::out.printWarning("Warning: Setting for interruptPin in gateway.conf is invalid.");
            _interruptPin = 2;
        }

        _transfer =  { (uint64_t)0, (uint64_t)0, (uint32_t)0, (uint32_t)4000000, (uint16_t)0, (uint8_t)8, (uint8_t)0, (uint32_t)0 };

        setConfig();

        start();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

MaxCc1101::~MaxCc1101()
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

void MaxCc1101::start()
{
    try
    {
        if(Gd::settings.device().empty())
        {
            Gd::out.printError("Error: No device defined for family MAX! CC1101. Please specify it in \"gateway.conf\".");
            return;
        }

        initDevice();

        _stopped = false;
        _firstPacket = true;
        _stopCallbackThread = false;
        Gd::bl->threadManager.start(_listenThread, true, 45, SCHED_FIFO, &MaxCc1101::mainThread, this);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::stop()
{
    try
    {
        _stopCallbackThread = true;
        Gd::bl->threadManager.join(_listenThread);
        _stopCallbackThread = false;
        if(_fileDescriptor->descriptor != -1) closeDevice();
        _gpio->closeDevice(Gd::settings.gpio1());
        _stopped = true;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::mainThread()
{
    try
    {
        int32_t pollResult;
        int32_t bytesRead;
        std::vector<char> readBuffer({'0'});

        while(!_stopCallbackThread)
        {
            try
            {
                if(_stopped)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }
                if(!_stopCallbackThread && (_fileDescriptor->descriptor == -1 || !_gpio->isOpen(Gd::settings.gpio1())))
                {
                    Gd::out.printError("Connection to TI CC1101 closed unexpectedly... Trying to reconnect...");
                    _stopped = true; //Set to true, so that sendPacket aborts
                    if(_sending)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        _sending = false;
                    }
                    _txMutex.unlock(); //Make sure _txMutex is unlocked

                    _gpio->closeDevice(Gd::settings.gpio1());
                    initDevice();
                    _stopped = false;
                    continue;
                }

                pollfd pollstruct {
                        (int)_gpio->getFileDescriptor(Gd::settings.gpio1())->descriptor,
                        (short)(POLLPRI | POLLERR),
                        (short)0
                };

                pollResult = poll(&pollstruct, 1, 100);
                /*if(pollstruct.revents & POLLERR)
                {
                    _out.printWarning("Warning: Error polling GPIO. Reopening...");
                    closeGPIO();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    openGPIO(_settings->gpio1);
                }*/
                if(pollResult > 0)
                {
                    if(lseek(_gpio->getFileDescriptor(Gd::settings.gpio1())->descriptor, 0, SEEK_SET) == -1) throw BaseLib::Exception("Could not poll gpio: " + std::string(strerror(errno)));
                    bytesRead = read(_gpio->getFileDescriptor(Gd::settings.gpio1())->descriptor, &readBuffer[0], 1);
                    if(!bytesRead) continue;
                    if(readBuffer.at(0) == 0x30)
                    {
                        if(!_sending) _txMutex.try_lock(); //We are receiving, don't send now
                        continue; //Packet is being received. Wait for GDO high
                    }
                    if(_sending)
                    {
                        endSending();
                        _txMutex.unlock();
                    }
                    else
                    {
                        //sendCommandStrobe(CommandStrobes::Enum::SIDLE);
                        std::string packet;
                        if(crcOK())
                        {
                            uint8_t firstByte = readRegister(Registers::Enum::FIFO);
                            std::vector<uint8_t> packetBytes = readRegisters(Registers::Enum::FIFO, firstByte + 1); //Read packet + RSSI
                            packetBytes[0] = firstByte;
                            if(packetBytes.size() > 100)
                            {
                                if(!_firstPacket)
                                {
                                    Gd::out.printWarning("Warning: Too large packet received: " + BaseLib::HelperFunctions::getHexString(packetBytes));
                                    closeDevice();
                                    _txMutex.unlock();
                                    continue;
                                }
                            }
                            else if(packetBytes.size() >= 9) packet = BaseLib::HelperFunctions::getHexString(packetBytes);
                            else Gd::out.printWarning("Warning: Too small packet received: " + BaseLib::HelperFunctions::getHexString(packetBytes));
                        }
                        else Gd::out.printDebug("Debug: MAX! packet received, but CRC failed.");
                        if(!_sendingPending)
                        {
                            sendCommandStrobe(CommandStrobes::Enum::SFRX);
                            sendCommandStrobe(CommandStrobes::Enum::SRX);
                        }
                        _txMutex.unlock();
                        if(!packet.empty())
                        {
                            if(_firstPacket) _firstPacket = false;
                            else
                            {
                                BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
                                parameters->reserve(2);
                                parameters->push_back(std::make_shared<BaseLib::Variable>(MAX_CC1101_FAMILY_ID));
                                parameters->push_back(std::make_shared<BaseLib::Variable>(packet));

                                if(_invoke)
                                {
                                    auto result = _invoke("packetReceived", parameters);
                                    if(result->errorStruct && result->structValue->at("faultCode")->integerValue != -1)
                                    {
                                        Gd::out.printError("Error calling packetReceived(): " + result->structValue->at("faultString")->stringValue);
                                    }
                                }
                            }
                        }
                    }
                }
                else if(pollResult < 0)
                {
                    _txMutex.unlock();
                    Gd::out.printError("Error: Could not poll gpio: " + std::string(strerror(errno)) + ". Reopening...");
                    _gpio->closeDevice(Gd::settings.gpio1());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    _gpio->openDevice(Gd::settings.gpio1(), true);
                }
                //pollResult == 0 is timeout
            }
            catch(const std::exception& ex)
            {
                _txMutex.unlock();
                Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    _txMutex.unlock();
}

void MaxCc1101::initDevice()
{
    try
    {
        openDevice();
        if(!_fileDescriptor || _fileDescriptor->descriptor == -1) return;

        initChip();
        Gd::out.printDebug("Debug: CC1100: Setting GPIO direction");
        int32_t gpioIndex = Gd::settings.gpio1();
        if(gpioIndex == -1)
        {
            Gd::out.printError("Error: GPIO 1 is not defined in settings.");
            return;
        }
        _gpio->setDirection(gpioIndex, BaseLib::LowLevel::Gpio::GpioDirection::Enum::IN);
        Gd::out.printDebug("Debug: CC1100: Setting GPIO edge");
        _gpio->setEdge(gpioIndex, BaseLib::LowLevel::Gpio::GpioEdge::Enum::BOTH);
        _gpio->openDevice(gpioIndex, true);
        if(!_gpio->isOpen(gpioIndex))
        {
            Gd::out.printError("Error: Couldn't listen to rf device, because the GPIO descriptor is not valid: " + Gd::settings.device());
            return;
        }
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::openDevice()
{
    try
    {
        if(_fileDescriptor && _fileDescriptor->descriptor != -1) closeDevice();

        _lockfile = Gd::bl->settings.lockFilePath() + "LCK.." + Gd::settings.device().substr(Gd::settings.device().find_last_of('/') + 1);
        int lockfileDescriptor = open(_lockfile.c_str(), O_WRONLY | O_EXCL | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if(lockfileDescriptor == -1)
        {
            if(errno != EEXIST)
            {
                Gd::out.printCritical("Couldn't create lockfile " + _lockfile + ": " + strerror(errno));
                return;
            }

            int processID = 0;
            std::ifstream lockfileStream(_lockfile.c_str());
            lockfileStream >> processID;
            if(getpid() != processID && kill(processID, 0) == 0)
            {
                Gd::out.printCritical("Rf device is in use: " + Gd::settings.device());
                return;
            }
            unlink(_lockfile.c_str());
            lockfileDescriptor = open(_lockfile.c_str(), O_WRONLY | O_EXCL | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            if(lockfileDescriptor == -1)
            {
                Gd::out.printCritical("Couldn't create lockfile " + _lockfile + ": " + strerror(errno));
                return;
            }
        }
        dprintf(lockfileDescriptor, "%10i", getpid());
        close(lockfileDescriptor);

        _fileDescriptor = _bl->fileDescriptorManager.add(open(Gd::settings.device().c_str(), O_RDWR | O_NONBLOCK));
        usleep(1000);

        if(_fileDescriptor->descriptor == -1)
        {
            Gd::out.printCritical("Couldn't open rf device \"" + Gd::settings.device() + "\": " + strerror(errno));
            return;
        }

        setupDevice();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::closeDevice()
{
    try
    {
        _bl->fileDescriptorManager.close(_fileDescriptor);
        unlink(_lockfile.c_str());
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::setupDevice()
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return;

        uint8_t mode = 0;
        uint8_t bits = 8;
        uint32_t speed = 4000000; //4MHz, see page 25 in datasheet

        if(ioctl(_fileDescriptor->descriptor, SPI_IOC_WR_MODE, &mode)) throw(BaseLib::Exception("Couldn't set spi mode on device " + Gd::settings.device()));
        if(ioctl(_fileDescriptor->descriptor, SPI_IOC_RD_MODE, &mode)) throw(BaseLib::Exception("Couldn't get spi mode off device " + Gd::settings.device()));

        if(ioctl(_fileDescriptor->descriptor, SPI_IOC_WR_BITS_PER_WORD, &bits)) throw(BaseLib::Exception("Couldn't set bits per word on device " + Gd::settings.device()));
        if(ioctl(_fileDescriptor->descriptor, SPI_IOC_RD_BITS_PER_WORD, &bits)) throw(BaseLib::Exception("Couldn't get bits per word off device " + Gd::settings.device()));

        if(ioctl(_fileDescriptor->descriptor, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) throw(BaseLib::Exception("Couldn't set speed on device " + Gd::settings.device()));
        if(ioctl(_fileDescriptor->descriptor, SPI_IOC_RD_MAX_SPEED_HZ, &speed)) throw(BaseLib::Exception("Couldn't get speed off device " + Gd::settings.device()));
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::initChip()
{
    try
    {
        if(_fileDescriptor->descriptor == -1)
        {
            Gd::out.printError("Error: Could not initialize TI CC1101. The spi device's file descriptor is not valid.");
            return;
        }
        reset();

        int32_t index = 0;
        for(std::vector<uint8_t>::const_iterator i = _config.begin(); i != _config.end(); ++i)
        {
            if(writeRegister((Registers::Enum)index, *i, true) != *i)
            {
                closeDevice();
                return;
            }
            index++;
        }
        if(writeRegister(Registers::Enum::FSTEST, 0x59, true) != 0x59)
        {
            closeDevice();
            return;
        }
        if(writeRegister(Registers::Enum::TEST2, 0x81, true) != 0x81) //Determined by SmartRF Studio
        {
            closeDevice();
            return;
        }
        if(writeRegister(Registers::Enum::TEST1, 0x35, true) != 0x35) //Determined by SmartRF Studio
        {
            closeDevice();
            return;
        }
        if(writeRegister(Registers::Enum::PATABLE, 0xC2, true) != 0xC2)
        {
            closeDevice();
            return;
        }

        sendCommandStrobe(CommandStrobes::Enum::SFRX);

        enableRX(true);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::reset()
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return;
        sendCommandStrobe(CommandStrobes::Enum::SRES);

        usleep(70); //Measured on HM-CC-VD
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::enableRX(bool flushRXFIFO)
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return;
        std::lock_guard<std::timed_mutex> txGuard(_txMutex);
        if(flushRXFIFO) sendCommandStrobe(CommandStrobes::Enum::SFRX);
        sendCommandStrobe(CommandStrobes::Enum::SRX);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void MaxCc1101::endSending()
{
    try
    {
        sendCommandStrobe(CommandStrobes::Enum::SIDLE);
        sendCommandStrobe(CommandStrobes::Enum::SFRX);
        sendCommandStrobe(CommandStrobes::Enum::SRX);
        _sending = false;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

bool MaxCc1101::crcOK()
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return false;
        std::vector<uint8_t> result = readRegisters(Registers::Enum::LQI, 1);
        if((result.size() == 2) && (result.at(1) & 0x80)) return true;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void MaxCc1101::readwrite(std::vector<uint8_t>& data)
{
    try
    {
        std::lock_guard<std::mutex> sendGuard(_sendMutex);
        _transfer.tx_buf = (uint64_t)&data[0];
        _transfer.rx_buf = (uint64_t)&data[0];
        _transfer.len = (uint32_t)data.size();
        if(_bl->debugLevel >= 6) Gd::out.printDebug("Debug: Sending: " + _bl->hf.getHexString(data));
        if(!ioctl(_fileDescriptor->descriptor, SPI_IOC_MESSAGE(1), &_transfer))
        {
            Gd::out.printError("Couldn't write to device " + Gd::settings.device() + ": " + std::string(strerror(errno)));
            return;
        }
        if(_bl->debugLevel >= 6) Gd::out.printDebug("Debug: Received: " + _bl->hf.getHexString(data));
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

bool MaxCc1101::checkStatus(uint8_t statusByte, Status::Enum status)
{
    try
    {
        if(_fileDescriptor->descriptor == -1 || !_gpio->isOpen(Gd::settings.gpio1())) return false;
        if((statusByte & (StatusBitmasks::Enum::CHIP_RDYn | StatusBitmasks::Enum::STATE)) != status) return false;
        return true;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

uint8_t MaxCc1101::readRegister(Registers::Enum registerAddress)
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return 0;
        std::vector<uint8_t> data({(uint8_t)(registerAddress | RegisterBitmasks::Enum::READ_SINGLE), 0x00});
        for(uint32_t i = 0; i < 5; i++)
        {
            readwrite(data);
            if(!(data.at(0) & StatusBitmasks::Enum::CHIP_RDYn)) break;
            data.at(0) = (uint8_t)(registerAddress  | RegisterBitmasks::Enum::READ_SINGLE);
            data.at(1) = 0;
            usleep(20);
        }
        return data.at(1);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return 0;
}

std::vector<uint8_t> MaxCc1101::readRegisters(Registers::Enum startAddress, uint8_t count)
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return std::vector<uint8_t>();
        std::vector<uint8_t> data({(uint8_t)(startAddress | RegisterBitmasks::Enum::READ_BURST)});
        data.resize(count + 1, 0);
        for(uint32_t i = 0; i < 5; i++)
        {
            readwrite(data);
            if(!(data.at(0) & StatusBitmasks::Enum::CHIP_RDYn)) break;
            data.clear();
            data.push_back((uint8_t)(startAddress  | RegisterBitmasks::Enum::READ_BURST));
            data.resize(count + 1, 0);
            usleep(20);
        }
        return data;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::vector<uint8_t>();
}

uint8_t MaxCc1101::writeRegister(Registers::Enum registerAddress, uint8_t value, bool check)
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return 0xFF;
        std::vector<uint8_t> data({(uint8_t)registerAddress, value});
        readwrite(data);
        if((data.at(0) & StatusBitmasks::Enum::CHIP_RDYn) || (data.at(1) & StatusBitmasks::Enum::CHIP_RDYn)) throw BaseLib::Exception("Error writing to register " + std::to_string(registerAddress) + ".");

        if(check)
        {
            data.at(0) = registerAddress | RegisterBitmasks::Enum::READ_SINGLE;
            data.at(1) = 0;
            readwrite(data);
            if(data.at(1) != value)
            {
                Gd::out.printError("Error (check) writing to register " + std::to_string(registerAddress) + ".");
                return 0;
            }
        }
        return value;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return 0;
}

void MaxCc1101::writeRegisters(Registers::Enum startAddress, std::vector<uint8_t>& values)
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return;
        std::vector<uint8_t> data({(uint8_t)(startAddress | RegisterBitmasks::Enum::WRITE_BURST) });
        data.insert(data.end(), values.begin(), values.end());
        readwrite(data);
        if((data.at(0) & StatusBitmasks::Enum::CHIP_RDYn)) Gd::out.printError("Error writing to registers " + std::to_string(startAddress) + ".");
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

uint8_t MaxCc1101::sendCommandStrobe(CommandStrobes::Enum commandStrobe)
{
    try
    {
        if(_fileDescriptor->descriptor == -1) return 0xFF;
        std::vector<uint8_t> data({(uint8_t)commandStrobe});
        for(uint32_t i = 0; i < 5; i++)
        {
            readwrite(data);
            if(!(data.at(0) & StatusBitmasks::Enum::CHIP_RDYn)) break;
            data.at(0) = (uint8_t)commandStrobe;
            usleep(20);
        }
        return data.at(0);
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return 0;
}

BaseLib::PVariable MaxCc1101::callMethod(std::string& method, BaseLib::PArray parameters)
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
BaseLib::PVariable MaxCc1101::sendPacket(BaseLib::PArray& parameters)
{
    try
    {
        if(parameters->size() != 3 || parameters->at(1)->type != BaseLib::VariableType::tString || parameters->at(1)->stringValue.empty() || parameters->at(2)->type != BaseLib::VariableType::tBoolean) return BaseLib::Variable::createError(-1, "Invalid parameters.");

        if(!_fileDescriptor || _fileDescriptor->descriptor == -1 || !_gpio->isOpen(Gd::settings.gpio1()) || _stopped) return BaseLib::Variable::createError(-1, "SPI device or GPIO is not open.");

        std::vector<uint8_t> packetBytes = _bl->hf.getUBinary(parameters->at(1)->stringValue);

        _sendingPending = true;
        _txMutex.lock();
        _sendingPending = false;
        if(_stopCallbackThread || _fileDescriptor->descriptor == -1 || !_gpio->isOpen(Gd::settings.gpio1()) || _stopped)
        {
            _txMutex.unlock();
            return BaseLib::Variable::createError(-1, "SPI device or GPIO is not open.");
        }
        _sending = true;
        sendCommandStrobe(CommandStrobes::Enum::SIDLE);
        sendCommandStrobe(CommandStrobes::Enum::SFTX);
        if(parameters->at(2)->booleanValue) //WOR packet
        {
            sendCommandStrobe(CommandStrobes::Enum::STX);
            usleep(1000000);
        }
        writeRegisters(Registers::Enum::FIFO, packetBytes);
        if(!parameters->at(2)->booleanValue) sendCommandStrobe(CommandStrobes::Enum::STX); //WOR packet?

        //Unlocking of _txMutex takes place in mainThread

        return std::make_shared<BaseLib::Variable>();
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void MaxCc1101::setConfig()
{
	if(_oscillatorFrequency == 26000000)
	{
		_config =
		{
			(_interruptPin == 2) ? (uint8_t)0x46 : (uint8_t)0x5B, //00: IOCFG2 (GDO2_CFG)
			0x2E, //01: IOCFG1 (GDO1_CFG to High impedance (3-state))
			(_interruptPin == 0) ? (uint8_t)0x46 : (uint8_t)0x5B, //02: IOCFG0 (GDO0_CFG)
			0x07, //03: FIFOTHR (FIFO threshold to 33 (TX) and 32 (RX)
			0xC6, //04: SYNC1
			0x26, //05: SYNC0
			0xFF, //06: PKTLEN (Maximum packet length)
			0x0C, //07: PKTCTRL1: CRC_AUTOFLUSH | APPEND_STATUS | NO_ADDR_CHECK
			0x45, //08: PKTCTRL0
			0x00, //09: ADDR
			0x00, //0A: CHANNR
			0x06, //0B: FSCTRL1
			0x00, //0C: FSCTRL0
			0x21, //0D: FREQ2
			0x65, //0E: FREQ1
			0x6A, //0F: FREQ0
			0xC8, //10: MDMCFG4
			0x93, //11: MDMCFG3
			0x03, //12: MDMCFG2
			0x22, //13: MDMCFG1
			0xF8, //14: MDMCFG0
			0x34, //15: DEVIATN
			0x07, //16: MCSM2
			0x30, //17: MCSM1: IDLE when packet has been received, RX after sending
			0x18, //18: MCSM0
			0x16, //19: FOCCFG
			0x6C, //1A: BSCFG
			0x03, //1B: AGCCTRL2
			0x40, //1C: AGCCTRL1
			0x91, //1D: AGCCTRL0
			0x87, //1E: WOREVT1
			0x6B, //1F: WOREVT0
			0xF8, //20: WORCRTL
			0x56, //21: FREND1
			0x10, //22: FREND0
			0xE9, //23: FSCAL3
			0x2A, //24: FSCAL2
			0x00, //25: FSCAL1
			0x1F, //26: FSCAL0
			0x41, //27: RCCTRL1
			0x00, //28: RCCTRL0
		};
	}
	else if(_oscillatorFrequency == 27000000)
	{
		_config =
		{
			(_interruptPin == 2) ? (uint8_t)0x46 : (uint8_t)0x5B, //00: IOCFG2 (GDO2_CFG)
			0x2E, //01: IOCFG1 (GDO1_CFG to High impedance (3-state))
			(_interruptPin == 0) ? (uint8_t)0x46 : (uint8_t)0x5B, //02: IOCFG0 (GDO0_CFG)
			0x07, //03: FIFOTHR (FIFO threshold to 33 (TX) and 32 (RX)
			0xC6, //04: SYNC1
			0x26, //05: SYNC0
			0xFF, //06: PKTLEN (Maximum packet length)
			0x0C, //07: PKTCTRL1: CRC_AUTOFLUSH | APPEND_STATUS | NO_ADDR_CHECK
			0x45, //08: PKTCTRL0
			0x00, //09: ADDR
			0x00, //0A: CHANNR
			0x06, //0B: FSCTRL1
			0x00, //0C: FSCTRL0
			0x20, //0D: FREQ2
			0x28, //0E: FREQ1
			0xC5, //0F: FREQ0
			0xC8, //10: MDMCFG4
			0x84, //11: MDMCFG3
			0x03, //12: MDMCFG2
			0x22, //13: MDMCFG1
			0xE5, //14: MDMCFG0
			0x34, //15: DEVIATN
			0x07, //16: MCSM2
			0x30, //17: MCSM1: IDLE when packet has been received, RX after sending
			0x18, //18: MCSM0
			0x16, //19: FOCCFG
			0x6C, //1A: BSCFG
			0x03, //1B: AGCCTRL2
			0x40, //1C: AGCCTRL1
			0x91, //1D: AGCCTRL0
			0x87, //1E: WOREVT1
			0x6B, //1F: WOREVT0
			0xF8, //20: WORCRTL
			0x56, //21: FREND1
			0x10, //22: FREND0
			0xE9, //23: FSCAL3
			0x2A, //24: FSCAL2
			0x00, //25: FSCAL1
			0x1F, //26: FSCAL0
			0x41, //27: RCCTRL1
			0x00, //28: RCCTRL0
		};
	}
	else Gd::out.printError("Error: Unknown value for \"oscillatorFrequency\" in max.conf. Valid values are 26000000 and 27000000.");
}
//}}}

#endif
