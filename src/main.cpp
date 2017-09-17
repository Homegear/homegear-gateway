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

#include "GD/GD.h"

#include <malloc.h>
#include <sys/prctl.h> //For function prctl
#include <sys/sysctl.h> //For BSD systems
#include <sys/resource.h> //getrlimit, setrlimit
#include <sys/file.h> //flock
#include <sys/types.h>
#include <sys/stat.h>

#include <gcrypt.h>
#include "../config.h"

void startUp();

GCRY_THREAD_OPTION_PTHREAD_IMPL;

bool _startAsDaemon = false;
std::mutex _shuttingDownMutex;
std::atomic_bool _reloading;
std::atomic_bool _startUpComplete;
std::atomic_bool _shutdownQueued;
bool _disposing = false;

void exitProgram(int exitCode)
{
    exit(exitCode);
}

void terminate(int signalNumber)
{
	try
	{
		if (signalNumber == SIGTERM || signalNumber == SIGINT)
		{
			_shuttingDownMutex.lock();
			if(!_startUpComplete)
			{
				GD::out.printMessage("Info: Startup is not complete yet. Queueing shutdown.");
				_shutdownQueued = true;
				_shuttingDownMutex.unlock();
				return;
			}
			if(GD::bl->shuttingDown)
			{
				_shuttingDownMutex.unlock();
				return;
			}
			GD::out.printMessage("(Shutdown) => Stopping Homegear InfluxDB (Signal: " + std::to_string(signalNumber) + ")");
			GD::bl->shuttingDown = true;
			_shuttingDownMutex.unlock();
			_disposing = true;
			GD::ipcClient->stop();
			GD::ipcClient.reset();
			GD::db.reset();
			GD::out.printMessage("(Shutdown) => Shutdown complete.");
			fclose(stdout);
			fclose(stderr);
			gnutls_global_deinit();
			gcry_control(GCRYCTL_SUSPEND_SECMEM_WARN);
			gcry_control(GCRYCTL_TERM_SECMEM);
			gcry_control(GCRYCTL_RESUME_SECMEM_WARN);
			exit(0);
		}
		else if(signalNumber == SIGHUP)
		{
			GD::out.printMessage("Info: SIGHUP received...");
			_shuttingDownMutex.lock();
			GD::out.printMessage("Info: Reloading...");
			if(!_startUpComplete)
			{
				_shuttingDownMutex.unlock();
				GD::out.printError("Error: Cannot reload. Startup is not completed.");
				return;
			}
			_startUpComplete = false;
			_shuttingDownMutex.unlock();
			if(!std::freopen((GD::settings.logfilePath() + "homegear-influxdb.log").c_str(), "a", stdout))
			{
				GD::out.printError("Error: Could not redirect output to new log file.");
			}
			if(!std::freopen((GD::settings.logfilePath() + "homegear-influxdb.err").c_str(), "a", stderr))
			{
				GD::out.printError("Error: Could not redirect errors to new log file.");
			}
			_shuttingDownMutex.lock();
			_startUpComplete = true;
			if(_shutdownQueued)
			{
				_shuttingDownMutex.unlock();
				terminate(SIGTERM);
			}
			_shuttingDownMutex.unlock();
			GD::out.printInfo("Info: Reload complete.");
		}
		else
		{
			if (!_disposing) GD::out.printCritical("Critical: Signal " + std::to_string(signalNumber) + " received. Stopping Homegear InfluxDB...");
			signal(signalNumber, SIG_DFL); //Reset signal handler for the current signal to default
			kill(getpid(), signalNumber); //Generate core dump
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

void getExecutablePath(int argc, char* argv[])
{
	char path[1024];
	if(!getcwd(path, sizeof(path)))
	{
		std::cerr << "Could not get working directory." << std::endl;
		exit(1);
	}
	GD::workingDirectory = std::string(path);
#ifdef KERN_PROC //BSD system
	int mib[4];
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	size_t cb = sizeof(path);
	int result = sysctl(mib, 4, path, &cb, NULL, 0);
	if(result == -1)
	{
		std::cerr << "Could not get executable path." << std::endl;
		exit(1);
	}
	path[sizeof(path) - 1] = '\0';
	GD::executablePath = std::string(path);
	GD::executablePath = GD::executablePath.substr(0, GD::executablePath.find_last_of("/") + 1);
#else
	int length = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (length < 0)
	{
		std::cerr << "Could not get executable path." << std::endl;
		exit(1);
	}
	if((unsigned)length > sizeof(path))
	{
		std::cerr << "The path the homegear binary is in has more than 1024 characters." << std::endl;
		exit(1);
	}
	path[length] = '\0';
	GD::executablePath = std::string(path);
	GD::executablePath = GD::executablePath.substr(0, GD::executablePath.find_last_of("/") + 1);
#endif

	GD::executableFile = std::string(argc > 0 ? argv[0] : "homegear");
	BaseLib::HelperFunctions::trim(GD::executableFile);
	if(GD::executableFile.empty()) GD::executableFile = "homegear";
	std::pair<std::string, std::string> pathNamePair = BaseLib::HelperFunctions::splitLast(GD::executableFile, '/');
	if(!pathNamePair.second.empty()) GD::executableFile = pathNamePair.second;
}

void initGnuTls()
{
	// {{{ Init gcrypt and GnuTLS
		gcry_error_t gcryResult;
		if((gcryResult = gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread)) != GPG_ERR_NO_ERROR)
		{
			GD::out.printCritical("Critical: Could not enable thread support for gcrypt.");
			exit(2);
		}

		if (!gcry_check_version(GCRYPT_VERSION))
		{
			GD::out.printCritical("Critical: Wrong gcrypt version.");
			exit(2);
		}
		gcry_control(GCRYCTL_SUSPEND_SECMEM_WARN);
		if((gcryResult = gcry_control(GCRYCTL_INIT_SECMEM, (int)GD::settings.secureMemorySize(), 0)) != GPG_ERR_NO_ERROR)
		{
			GD::out.printCritical("Critical: Could not allocate secure memory. Error code is: " + std::to_string((int32_t)gcryResult));
			exit(2);
		}
		gcry_control(GCRYCTL_RESUME_SECMEM_WARN);
		gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

		int32_t gnutlsResult = 0;
		if((gnutlsResult = gnutls_global_init()) != GNUTLS_E_SUCCESS)
		{
			GD::out.printCritical("Critical: Could not initialize GnuTLS: " + std::string(gnutls_strerror(gnutlsResult)));
			exit(2);
		}
	// }}}
}

void setLimits()
{
	struct rlimit limits;
	if(!GD::settings.enableCoreDumps()) prctl(PR_SET_DUMPABLE, 0);
	else
	{
		//Set rlimit for core dumps
		getrlimit(RLIMIT_CORE, &limits);
		limits.rlim_cur = limits.rlim_max;
		GD::out.printInfo("Info: Setting allowed core file size to \"" + std::to_string(limits.rlim_cur) + "\" for user with id " + std::to_string(getuid()) + " and group with id " + std::to_string(getgid()) + '.');
		setrlimit(RLIMIT_CORE, &limits);
		getrlimit(RLIMIT_CORE, &limits);
		GD::out.printInfo("Info: Core file size now is \"" + std::to_string(limits.rlim_cur) + "\".");
	}
}

void printHelp()
{
	std::cout << "Usage: homegear [OPTIONS]" << std::endl << std::endl;
	std::cout << "Option              Meaning" << std::endl;
	std::cout << "-h                  Show this help" << std::endl;
	std::cout << "-u                  Run as user" << std::endl;
	std::cout << "-g                  Run as group" << std::endl;
	std::cout << "-c <path>           Specify path to config file" << std::endl;
	std::cout << "-d                  Run as daemon" << std::endl;
	std::cout << "-p <pid path>       Specify path to process id file" << std::endl;
	std::cout << "-v                  Print program version" << std::endl;
}

void startDaemon()
{
	try
	{
		pid_t pid, sid;
		pid = fork();
		if(pid < 0)
		{
			exitProgram(1);
		}
		if(pid > 0)
		{
			exitProgram(0);
		}

		//Set process permission
		umask(S_IWGRP | S_IWOTH);

		//Set child processe's id
		sid = setsid();
		if(sid < 0)
		{
			exitProgram(1);
		}

		close(STDIN_FILENO);
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

void startUp()
{
	try
	{
		if((chdir(GD::settings.workingDirectory().c_str())) < 0)
		{
			GD::out.printError("Could not change working directory to " + GD::settings.workingDirectory() + ".");
			exitProgram(1);
		}

    	struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = terminate;

    	//Use sigaction over signal because of different behavior in Linux and BSD
    	sigaction(SIGHUP, &sa, NULL);
    	sigaction(SIGTERM, &sa, NULL);
    	sigaction(SIGABRT, &sa, NULL);
    	sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);

		if(!std::freopen((GD::settings.logfilePath() + "homegear-influxdb.log").c_str(), "a", stdout))
		{
			GD::out.printError("Error: Could not redirect output to log file.");
		}
		if(!std::freopen((GD::settings.logfilePath() + "homegear-influxdb.err").c_str(), "a", stderr))
		{
			GD::out.printError("Error: Could not redirect errors to log file.");
		}

    	GD::out.printMessage("Starting Homegear InfluxDB...");

    	if(GD::settings.memoryDebugging()) mallopt(M_CHECK_ACTION, 3); //Print detailed error message, stack trace, and memory, and abort the program. See: http://man7.org/linux/man-pages/man3/mallopt.3.html

    	initGnuTls();

		if(!GD::bl->io.directoryExists(GD::settings.socketPath()))
		{
			if(!GD::bl->io.createDirectory(GD::settings.socketPath(), S_IRWXU | S_IRWXG))
			{
				GD::out.printCritical("Critical: Directory \"" + GD::settings.socketPath() + "\" does not exist and cannot be created.");
				exit(1);
			}
			if(GD::bl->userId != 0 || GD::bl->groupId != 0)
			{
				if(chown(GD::settings.socketPath().c_str(), GD::bl->userId, GD::bl->groupId) == -1)
				{
					GD::out.printCritical("Critical: Could not set permissions on directory \"" + GD::settings.socketPath() + "\"");
					exit(1);
				}
			}
		}

		setLimits();

    	if(getuid() == 0 && !GD::runAsUser.empty() && !GD::runAsGroup.empty())
    	{
			if(GD::bl->userId == 0 || GD::bl->groupId == 0)
			{
				GD::out.printCritical("Could not drop privileges. User name or group name is not valid.");
				exitProgram(1);
			}
			GD::out.printInfo("Info: Dropping privileges to user " + GD::runAsUser + " (" + std::to_string(GD::bl->userId) + ") and group " + GD::runAsGroup + " (" + std::to_string(GD::bl->groupId) + ")");

			int result = -1;
			std::vector<gid_t> supplementaryGroups(10);
			int numberOfGroups = 10;
			while(result == -1)
			{
				result = getgrouplist(GD::runAsUser.c_str(), 10000, supplementaryGroups.data(), &numberOfGroups);

				if(result == -1) supplementaryGroups.resize(numberOfGroups);
				else supplementaryGroups.resize(result);
			}

			if(setgid(GD::bl->groupId) != 0)
			{
				GD::out.printCritical("Critical: Could not drop group privileges.");
				exitProgram(1);
			}

			if(setgroups(supplementaryGroups.size(), supplementaryGroups.data()) != 0)
			{
				GD::out.printCritical("Critical: Could not set supplementary groups: " + std::string(strerror(errno)));
				exitProgram(1);
			}

			if(setuid(GD::bl->userId) != 0)
			{
				GD::out.printCritical("Critical: Could not drop user privileges.");
				exitProgram(1);
			}

			//Core dumps are disabled by setuid. Enable them again.
			if(GD::settings.enableCoreDumps()) prctl(PR_SET_DUMPABLE, 1);
    	}

    	if(getuid() == 0)
    	{
    		if(!GD::runAsUser.empty() && !GD::runAsGroup.empty())
    		{
    			GD::out.printCritical("Critical: Homegear still has root privileges though privileges should have been dropped. Exiting Homegear as this is a security risk.");
				exit(1);
    		}
    		else GD::out.printWarning("Warning: Running as root. The authors of Homegear recommend running Homegear as user.");
    	}
    	else
    	{
    		if(setuid(0) != -1)
			{
				GD::out.printCritical("Critical: Regaining root privileges succeded. Exiting Homegear as this is a security risk.");
				exit(1);
			}
    		GD::out.printInfo("Info: Homegear is (now) running as user with id " + std::to_string(getuid()) + " and group with id " + std::to_string(getgid()) + '.');
    	}

    	//Create PID file
    	try
    	{
			if(!GD::pidfilePath.empty())
			{
				int32_t pidfile = open(GD::pidfilePath.c_str(), O_CREAT | O_RDWR, 0666);
				if(pidfile < 0)
				{
					GD::out.printError("Error: Cannot create pid file \"" + GD::pidfilePath + "\".");
				}
				else
				{
					int32_t rc = flock(pidfile, LOCK_EX | LOCK_NB);
					if(rc && errno == EWOULDBLOCK)
					{
						GD::out.printError("Error: Homegear is already running - Can't lock PID file.");
					}
					std::string pid(std::to_string(getpid()));
					int32_t bytesWritten = write(pidfile, pid.c_str(), pid.size());
					if(bytesWritten <= 0) GD::out.printError("Error writing to PID file: " + std::string(strerror(errno)));
					close(pidfile);
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

		while(BaseLib::HelperFunctions::getTime() < 1000000000000)
		{
			GD::out.printWarning("Warning: Time is in the past. Waiting for ntp to set the time...");
			std::this_thread::sleep_for(std::chrono::milliseconds(10000));
		}

		GD::db.reset(new Database(GD::bl.get()));
		while(!_shutdownQueued)
		{
			if(GD::db->open()) break;
			GD::out.printWarning("Warning: Could not connect to InfluxdB. Please make sure it is running and your settings are correct.");
			std::this_thread::sleep_for(std::chrono::milliseconds(10000));
		}

		GD::ipcClient.reset(new IpcClient(GD::settings.socketPath() + "homegearIPC.sock"));
		if(!_shutdownQueued) GD::ipcClient->start();

        GD::out.printMessage("Startup complete.");

        GD::bl->booting = false;

        _shuttingDownMutex.lock();
		_startUpComplete = true;
		if(_shutdownQueued)
		{
			_shuttingDownMutex.unlock();
			terminate(SIGTERM);
		}
		_shuttingDownMutex.unlock();

		if(BaseLib::Io::fileExists(GD::settings.workingDirectory() + "core"))
		{
			GD::out.printError("Error: A core file exists in Homegear InfluxDB's working directory (\"" + GD::settings.workingDirectory() + "core" + "\"). Please send this file to the Homegear team including information about your system (Linux distribution, CPU architecture), the Homegear InfluxDB version, the current log files and information what might've caused the error.");
		}

       	while(true) std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        terminate(SIGTERM);
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

int main(int argc, char* argv[])
{
	try
    {
		_reloading = false;
		_startUpComplete = false;
		_shutdownQueued = false;

    	getExecutablePath(argc, argv);
    	GD::bl.reset(new BaseLib::SharedObjects());
    	GD::out.init(GD::bl.get());

		if(BaseLib::Io::directoryExists(GD::executablePath + "config")) GD::configPath = GD::executablePath + "config/";
		else if(BaseLib::Io::directoryExists(GD::executablePath + "cfg")) GD::configPath = GD::executablePath + "cfg/";
		else GD::configPath = "/etc/homegear/";

    	if(std::string(VERSION) != GD::bl->version())
    	{
    		GD::out.printCritical(std::string("Base library has wrong version. Expected version ") + VERSION + " but got version " + GD::bl->version());
    		exit(1);
    	}

    	for(int32_t i = 1; i < argc; i++)
    	{
    		std::string arg(argv[i]);
    		if(arg == "-h" || arg == "--help")
    		{
    			printHelp();
    			exit(0);
    		}
    		else if(arg == "-c")
    		{
    			if(i + 1 < argc)
    			{
    				std::string configPath = std::string(argv[i + 1]);
    				if(!configPath.empty()) GD::configPath = configPath;
    				if(GD::configPath[GD::configPath.size() - 1] != '/') GD::configPath.push_back('/');
    				i++;
    			}
    			else
    			{
    				printHelp();
    				exit(1);
    			}
    		}
    		else if(arg == "-p")
    		{
    			if(i + 1 < argc)
    			{
    				GD::pidfilePath = std::string(argv[i + 1]);
    				i++;
    			}
    			else
    			{
    				printHelp();
    				exit(1);
    			}
    		}
    		else if(arg == "-u")
    		{
    			if(i + 1 < argc)
    			{
    				GD::runAsUser = std::string(argv[i + 1]);
    				i++;
    			}
    			else
    			{
    				printHelp();
    				exit(1);
    			}
    		}
    		else if(arg == "-g")
    		{
    			if(i + 1 < argc)
    			{
    				GD::runAsGroup = std::string(argv[i + 1]);
    				i++;
    			}
    			else
    			{
    				printHelp();
    				exit(1);
    			}
    		}
    		else if(arg == "-d")
    		{
    			_startAsDaemon = true;
    		}
    		else if(arg == "-v")
    		{
    			std::cout << "Homegear InfluxDB version " << VERSION << std::endl;
    			std::cout << "Copyright (c) 2013-2017 Sathya Laufer" << std::endl << std::endl;
    			exit(0);
    		}
    		else
    		{
    			printHelp();
    			exit(1);
    		}
    	}

    	// {{{ Load settings
			GD::out.printInfo("Loading settings from " + GD::configPath + "influxdb.conf");
			GD::settings.load(GD::configPath + "influxdb.conf", GD::executablePath);
			if(GD::runAsUser.empty()) GD::runAsUser = GD::settings.runAsUser();
			if(GD::runAsGroup.empty()) GD::runAsGroup = GD::settings.runAsGroup();
			if((!GD::runAsUser.empty() && GD::runAsGroup.empty()) || (!GD::runAsGroup.empty() && GD::runAsUser.empty()))
			{
				GD::out.printCritical("Critical: You only provided a user OR a group for Homegear InfluxDB to run as. Please specify both.");
				exit(1);
			}
			GD::bl->userId = GD::bl->hf.userId(GD::runAsUser);
			GD::bl->groupId = GD::bl->hf.groupId(GD::runAsGroup);
			if((int32_t)GD::bl->userId == -1 || (int32_t)GD::bl->groupId == -1)
			{
				GD::bl->userId = 0;
				GD::bl->groupId = 0;
			}
		// }}}

		if((chdir(GD::settings.workingDirectory().c_str())) < 0)
		{
			GD::out.printError("Could not change working directory to " + GD::settings.workingDirectory() + ".");
			exitProgram(1);
		}

		if(_startAsDaemon) startDaemon();
    	startUp();

        return 0;
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
	terminate(SIGTERM);

    return 1;
}
