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

#ifndef HOMEGEAR_GATEWAY_CC110L_TEST_H
#define HOMEGEAR_GATEWAY_CC110L_TEST_H

#ifdef SPISUPPORT

#include "ICommunicationInterface.h"

#define CC110L_TEST_FAMILY_ID 0

class Cc110LTest : public ICommunicationInterface
{
public:
    Cc110LTest(BaseLib::SharedObjects* bl);
    virtual ~Cc110LTest();
    virtual BaseLib::PVariable callMethod(std::string& method, BaseLib::PArray parameters);
private:
    struct CommandStrobes
    {
        enum Enum
        {
            SRES =		0x30, // Reset chip.
            SFSTXON =	0x31, // Enable and calibrate frequency synthesizer (if MCSM0.FS_AUTOCAL=1). If in RX (with CCA):
            // Go to a wait state where only the synthesizer is running (for quick RX / TX turnaround).
                    SXOFF =		0x32, // Turn off crystal oscillator.
            SCAL =		0x33, // Calibrate frequency synthesizer and turn it off. SCAL can be strobed from IDLE mode without
            // setting manual calibration mode (MCSM0.FS_AUTOCAL=0)
                    SRX =		0x34, // Enable RX. Perform calibration first if coming from IDLE and MCSM0.FS_AUTOCAL=1.
            STX =		0x35, // In IDLE state: Enable TX. Perform calibration first if MCSM0.FS_AUTOCAL=1.
            // If in RX state and CCA is enabled: Only go to TX if channel is clear.
                    SIDLE =		0x36, // Exit RX / TX, turn off frequency synthesizer and exit Wake-On-Radio mode if applicable.
            SWOR =		0x38, // Start automatic RX polling sequence (Wake-on-Radio) as described in Section 19.5 if
            // WORCTRL.RC_PD=0.
                    SPWD =		0x39, // Enter power down mode when CSn goes high.
            SFRX =		0x3A, // Flush the RX FIFO buffer. Only issue SFRX in IDLE or, RXFIFO_OVERFLOW states.
            SFTX =		0x3B, // Flush the TX FIFO buffer. Only issue SFTX in IDLE or TXFIFO_UNDERFLOW states.
            SWORRST =	0x3C, // Reset real time clock to Event1 value.
            SNOP =		0x3D  // No operation. May be used to get access to the chip status byte.
        };
    };

    struct Status
    {
        enum Enum
        {
            IDLE = 				0x00,
            RX =				0x10,
            TX =				0x20,
            FSTXON = 			0x30,
            CALIBRATE =			0x40,
            SETTLING = 			0x50,
            RXFIFO_OVERFLOW = 	0x60,
            TXFIFO_OVERFLOW =	0x70
        };
    };

    struct StatusBitmasks
    {
        enum Enum
        {
            CHIP_RDYn =		0x80,
            STATE =			0x70,
            FIFO_BYTES_AVAILABLE = 0x0F
        };
    };

    struct RegisterBitmasks
    {
        enum Enum
        {
            WRITE_BURST =	0x40,
            READ_SINGLE =	0x80,
            READ_BURST = 	0xC0
        };
    };

    struct Registers
    {
        enum Enum
        {
            IOCFG2 =		0x00,
            IOCFG1 = 		0x01,
            IOCFG0 =		0x02,
            FIFOTHR =		0x03,
            SYNC1 =			0x04,
            SYNC2 =			0x05,
            PKTLEN =		0x06,
            PKTCTRL1 =		0x07,
            PKTCTRL0 =		0x08,
            ADDR =			0x09,
            CHANNR =		0x0A,
            FSCTRL1 =		0x0B,
            FSCTRL0 =		0x0C,
            FREQ2 =			0x0D,
            FREQ1 =			0x0E,
            FREQ0 =			0x0F,
            MDMCFG4 =		0x10,
            MDMCFG3 =		0x11,
            MDMCFG2 =		0x12,
            MDMCFG1 =		0x13,
            MDMCFG0 =		0x14,
            DEVIATN =		0x15,
            MCSM2 =			0x16,
            MCSM1 =			0x17,
            MCSM0 =			0x18,
            FOCCFG =		0x19,
            BSCFG =			0x1A,
            AGCCTRL2 =		0x1B,
            AGCCTRL1 =		0x1C,
            AGCCTRL0 =		0x1D,
            UNUSED1 =		0x1E,
            UNUSED2 =		0x1F,
            RESERVED1 =		0x20,
            FREND1 =		0x21,
            FREND0 =		0x22,
            FSCAL3 =		0x23,
            FSCAL2 =		0x24,
            FSCAL1 =		0x25,
            FSCAL0 =		0x26,
            UNUSED3 =		0x27,
            UNUSED4 =		0x28,
            RESERVED2 =		0x29,
            RESERVED3 =		0x2A,
            RESERVED4 =		0x2B,
            TEST2 =			0x2C,
            TEST1 =			0x2D,
            TEST0 =			0x2E,
            //No 0x2F
            PARTNUM =		0x30,
            CHIPVERSION =	0x31,
            FREQTEST =		0x32,
            LQI =			0x33,
            RSSI =			0x34,
            MARCSTATE =		0x35,
            RESERVED5 =		0x36,
            RESERVED6 =		0x37,
            PKTSTATUS =		0x38,
            RESERVED7 =	    0x39,
            TXBYTES =		0x3A,
            RXBYTES =		0x3B,
            RESERVED8 =     0x3C,
            RESERVED9 =     0x3D,
            PATABLE =		0x3E,
            FIFO =			0x3F
        };
    };

    BaseLib::PFileDescriptor _fileDescriptor;
    std::unique_ptr<BaseLib::LowLevel::Gpio> _gpio;
    std::string _lockfile;
    int32_t _oscillatorFrequency = 26000000;
    int32_t _interruptPin = 2;
    std::atomic_bool _updateMode;
    std::vector<uint8_t> _config;
    std::vector<uint8_t> _patable;
    struct spi_ioc_transfer _transfer;
    std::timed_mutex _txMutex;
    std::mutex _sendMutex;
    std::atomic_bool _sending;
    std::atomic_bool _sendingPending;
    std::atomic_bool _firstPacket;
    std::thread _listenThread;
    std::atomic_bool _stopped;
    std::atomic_bool _stopCallbackThread;

    std::atomic_bool _stopTxThread;
    std::thread _txThread;

    void start();
    void stop();

    void mainThread();
    void txThread();

    void setConfig();
    void setupDevice();
    void initDevice();
    void openDevice();
    void closeDevice();
    void initChip();
    void reset();
    bool crcOK();
    void enableRX(bool flushRXFIFO);
    void endSending();

    void readwrite(std::vector<uint8_t>& data);
    uint8_t sendCommandStrobe(CommandStrobes::Enum commandStrobe);
    uint8_t readRegister(Registers::Enum registerAddress);
    std::vector<uint8_t> readRegisters(Registers::Enum startAddress, uint8_t count);
    uint8_t writeRegister(Registers::Enum registerAddress, uint8_t value, bool check = false);
    void writeRegisters(Registers::Enum startAddress, std::vector<uint8_t>& values);
    bool checkStatus(uint8_t statusByte, Status::Enum status);

//{{{ RPC methods
    BaseLib::PVariable sendPacket(BaseLib::PArray& parameters);
    BaseLib::PVariable startTx(BaseLib::PArray& parameters);
    BaseLib::PVariable stopTx(BaseLib::PArray& parameters);
//}}}
};

#endif
#endif