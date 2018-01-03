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

#ifndef GATEWAYSETTINGS_H_
#define GATEWAYSETTINGS_H_

#include <homegear-base/BaseLib.h>

class Settings
{
public:
	Settings();
	virtual ~Settings() {}
	void load(std::string filename, std::string executablePath);
	bool changed();

	std::string listenAddress() { return _listenAddress; }
	int32_t port() { return _port; }
	std::string runAsUser() { return _runAsUser; }
	std::string runAsGroup() { return _runAsGroup; }
	int32_t debugLevel() { return _debugLevel; }
	bool memoryDebugging() { return _memoryDebugging; }
	bool enableCoreDumps() { return _enableCoreDumps; };
	std::string workingDirectory() { return _workingDirectory; }
	std::string logfilePath() { return _logfilePath; }
	uint32_t secureMemorySize() { return _secureMemorySize; }
	std::string caFile() { return _caFile; }
	std::string certPath() { return _certPath; }
	std::string keyPath() { return _keyPath; }
	std::string dhPath() { return _dhPath; }

    bool enableUpnp() { return _enableUpnp; }
    std::string upnpIpAddress() { return _upnpIpAddress; }
    std::string upnpUdn() { return _upnpUdn; }

    std::string family() { return _family; }
    std::string device() { return _device; }
	int32_t gpio1() { return _gpio1; }
    int32_t gpio2() { return _gpio2; }
private:
	std::string _executablePath;
	std::string _path;
	int32_t _lastModified = -1;

	std::string _listenAddress;
	int32_t _port = 2017;
	std::string _runAsUser;
	std::string _runAsGroup;
	int32_t _debugLevel = 3;
	bool _memoryDebugging = false;
	bool _enableCoreDumps = true;
	std::string _workingDirectory;
	std::string _logfilePath;
	uint32_t _secureMemorySize = 65536;
	std::string _caFile;
	std::string _certPath;
	std::string _keyPath;
	std::string _dhPath;

    bool _enableUpnp = false;
    std::string _upnpIpAddress;
    std::string _upnpUdn;

	std::string _family;
    std::string _device;
	int32_t _gpio1 = -1;
	int32_t _gpio2 = -1;

	void reset();
};
#endif
