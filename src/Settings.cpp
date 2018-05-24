/* Copyright 2013-2017 Sathya Laufer
 *
 * libhomegear-base is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * libhomegear-base is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libhomegear-base.  If not, see
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

#include "Settings.h"
#include "GD.h"

Settings::Settings()
{
}

void Settings::reset()
{
	_listenAddress = "::";
	_port = 2017;
	_portUnconfigured = 2018;
	_runAsUser = "";
	_runAsGroup = "";
	_debugLevel = 3;
	_memoryDebugging = false;
	_enableCoreDumps = true;
	_workingDirectory = _executablePath;
	_logFilePath = "/var/log/homegear-gateway/";
    _dataPath = "/var/lib/homegear-gateway/";
	_lockFilePath = "/var/lock/";
	_gpioPath = "/sys/class/gpio/";
	_secureMemorySize = 65536;
	_caFile = "";
	_certPath = "";
	_keyPath = "";
	_dhPath = "";

	_configurationPassword = "";

    _enableUpnp = true;
    _upnpIpAddress = "";
    _upnpUdn = "";

    _family = "";
    _device = "";
	_gpio1 = -1;
	_gpio2 = -1;
    _oscillatorFrequency = -1;
    _interruptPin = -1;
}

bool Settings::changed()
{
	if(GD::bl->io.getFileLastModifiedTime(_path) != _lastModified)
	{
		return true;
	}
	return false;
}

void Settings::load(std::string filename, std::string executablePath)
{
	try
	{
		_executablePath = executablePath;
		reset();
		_path = filename;
		char input[1024];
		FILE *fin;
		int32_t len, ptr;
		bool found = false;

		if (!(fin = fopen(filename.c_str(), "r")))
		{
			GD::bl->out.printError("Unable to open config file: " + filename + ". " + strerror(errno));
			return;
		}

		while (fgets(input, 1024, fin))
		{
			if(input[0] == '#') continue;
			len = strlen(input);
			if (len < 2) continue;
			if (input[len-1] == '\n') input[len-1] = '\0';
			ptr = 0;
			found = false;
			while(ptr < len)
			{
				if (input[ptr] == '=')
				{
					found = true;
					input[ptr++] = '\0';
					break;
				}
				ptr++;
			}
			if(found)
			{
				std::string name(input);
				BaseLib::HelperFunctions::toLower(name);
				BaseLib::HelperFunctions::trim(name);
				std::string value(&input[ptr]);
				BaseLib::HelperFunctions::trim(value);
				if(name == "listenaddress")
				{
					_listenAddress = value;
					if(_listenAddress.empty()) _listenAddress = "::";
					GD::bl->out.printDebug("Debug: listenAddress set to " + _listenAddress);
				}
				else if(name == "port")
				{
					_port = BaseLib::Math::getNumber(value);
					if(_port < 1 || _port > 65535) _port = 2017;
					GD::bl->out.printDebug("Debug: port set to " + std::to_string(_port));
				}
				else if(name == "portunconfigured")
				{
					_portUnconfigured = BaseLib::Math::getNumber(value);
					if(_portUnconfigured < 1 || _portUnconfigured > 65535) _portUnconfigured = 2018;
					GD::bl->out.printDebug("Debug: portUnconfigured set to " + std::to_string(_portUnconfigured));
				}
				else if(name == "runasuser")
				{
					_runAsUser = value;
					GD::bl->out.printDebug("Debug: runAsUser set to " + _runAsUser);
				}
				else if(name == "runasgroup")
				{
					_runAsGroup = value;
					GD::bl->out.printDebug("Debug: runAsGroup set to " + _runAsGroup);
				}
				else if(name == "debuglevel")
				{
					_debugLevel = BaseLib::Math::getNumber(value);
					if(_debugLevel < 0) _debugLevel = 3;
					GD::bl->debugLevel = _debugLevel;
					GD::bl->out.printDebug("Debug: debugLevel set to " + std::to_string(_debugLevel));
				}
				else if(name == "memorydebugging")
				{
					if(BaseLib::HelperFunctions::toLower(value) == "true") _memoryDebugging = true;
					GD::bl->out.printDebug("Debug: memoryDebugging set to " + std::to_string(_memoryDebugging));
				}
				else if(name == "enablecoredumps")
				{
					if(BaseLib::HelperFunctions::toLower(value) == "false") _enableCoreDumps = false;
					GD::bl->out.printDebug("Debug: enableCoreDumps set to " + std::to_string(_enableCoreDumps));
				}
				else if(name == "workingdirectory")
				{
					_workingDirectory = value;
					if(_workingDirectory.empty()) _workingDirectory = _executablePath;
					if(_workingDirectory.back() != '/') _workingDirectory.push_back('/');
					GD::bl->out.printDebug("Debug: workingDirectory set to " + _workingDirectory);
				}
				else if(name == "logfilepath")
				{
					_logFilePath = value;
					if(_logFilePath.empty()) _logFilePath = "/var/log/homegear-gateway/";
					if(_logFilePath.back() != '/') _logFilePath.push_back('/');
					GD::bl->out.printDebug("Debug: logfilePath set to " + _logFilePath);
				}
                else if(name == "datapath")
                {
                    _dataPath = value;
                    if(_dataPath.empty()) _dataPath = "/var/lib/homegear-gateway/";
                    if(_dataPath.back() != '/') _dataPath.push_back('/');
                    GD::bl->out.printDebug("Debug: dataPath set to " + _dataPath);
                }
				else if(name == "lockfilepath")
				{
					_lockFilePath = value;
					if(_lockFilePath.empty()) _lockFilePath = "/var/lock/";
					if(_lockFilePath.back() != '/') _lockFilePath.push_back('/');
					GD::bl->settings.setLockFilePath(_lockFilePath);
					GD::bl->out.printDebug("Debug: lockfilePath set to " + _lockFilePath);
				}
                else if(name == "gpiopath")
                {
                    _gpioPath = value;
                    if(_gpioPath.empty()) _gpioPath = "/sys/class/gpio/";
                    if(_gpioPath.back() != '/') _gpioPath.push_back('/');
                    GD::bl->out.printDebug("Debug: gpioPath set to " + _gpioPath);
                }
				else if(name == "securememorysize")
				{
					_secureMemorySize = BaseLib::Math::getNumber(value);
					//Allow 0 => disable secure memory. 16384 is minimum size. Values smaller than 16384 are set to 16384 by gcrypt: https://gnupg.org/documentation/manuals/gcrypt-devel/Controlling-the-library.html
					if(_secureMemorySize < 0) _secureMemorySize = 1;
					GD::bl->out.printDebug("Debug: secureMemorySize set to " + std::to_string(_secureMemorySize));
				}
				else if(name == "cafile")
				{
					_caFile = value;
					GD::bl->out.printDebug("Debug: caFile set to " + _caFile);
				}
				else if(name == "certpath")
				{
					_certPath = value;
					GD::bl->out.printDebug("Debug: certPath set to " + _certPath);
				}
				else if(name == "keypath")
				{
					_keyPath = value;
					GD::bl->out.printDebug("Debug: keyPath set to " + _keyPath);
				}
				else if(name == "dhpath")
				{
					_dhPath = value;
					GD::bl->out.printDebug("Debug: dhPath set to " + _dhPath);
				}
				else if(name == "configurationpassword")
				{
					_configurationPassword = value;
					GD::bl->out.printDebug("Debug: configurationPassword was set");
				}
                else if(name == "enableupnp")
                {
                    _enableUpnp = BaseLib::HelperFunctions::toLower(value) == "true";
                    GD::out.printDebug("Debug: enableUPnP set to " + std::to_string(_enableUpnp));
                }
                else if(name == "upnpipaddress")
                {
                    _upnpIpAddress = value;
                    GD::out.printDebug("Debug: uPnPIpAddress set to " + _upnpIpAddress);
                }
                else if(name == "upnpudn")
                {
                    _upnpUdn = value;
                    GD::out.printDebug("Debug: uPnPUDN set to " + _upnpUdn);
                }
                else if(name == "family")
                {
                    _family = BaseLib::HelperFunctions::toLower(value);
                    GD::bl->out.printDebug("Debug: family set to " + _family);
                }
                else if(name == "device")
                {
                    _device = value;
                    GD::bl->out.printDebug("Debug: device set to " + _device);
                }
				else if(name == "gpio1")
				{
					_gpio1 = BaseLib::Math::getNumber(value);
					if(_gpio1 < 0) _gpio1 =  -1;
					GD::bl->out.printDebug("Debug: gpio1 set to " + std::to_string(_gpio1));
				}
				else if(name == "gpio2")
				{
					_gpio2 = BaseLib::Math::getNumber(value);
					if(_gpio2 < 0) _gpio2 =  -1;
					GD::bl->out.printDebug("Debug: gpio2 set to " + std::to_string(_gpio2));
				}
				else if(name == "oscillatorfrequency")
				{
					_oscillatorFrequency = BaseLib::Math::getNumber(value);
					if(_oscillatorFrequency < 0) _oscillatorFrequency = -1;
					GD::bl->out.printDebug("Debug: oscillatorFrequency set to " + std::to_string(_oscillatorFrequency));
				}
				else if(name == "interruptpin")
				{
					int32_t number = BaseLib::Math::getNumber(value);
					if(number >= 0)
					{
						_interruptPin = number;
						GD::bl->out.printDebug("Debug: interruptPin set to " + std::to_string(_interruptPin));
					}
				}
				else
				{
					GD::bl->out.printWarning("Warning: Setting not found: " + std::string(input));
				}
			}
		}

		fclose(fin);
		_lastModified = GD::bl->io.getFileLastModifiedTime(filename);
	}
	catch(const std::exception& ex)
    {
		GD::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(const BaseLib::Exception& ex)
    {
    	GD::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}
