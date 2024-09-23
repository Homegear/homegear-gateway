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

#include "Gd.h"

#include <iostream>

#include <malloc.h>
#include <sys/prctl.h> //For function prctl
#ifdef BSDSYSTEM
#include <sys/sysctl.h> //For BSD systems
#endif
#include <sys/resource.h> //getrlimit, setrlimit
#include <sys/file.h> //flock
#include <sys/types.h>
#include <sys/stat.h>

#include <gcrypt.h>
#include <grp.h>
#include "../config.h"

void startUp();

GCRY_THREAD_OPTION_PTHREAD_IMPL;

bool _startAsDaemon = false;
bool _txTestMode = false;
std::mutex _shuttingDownMutex;
std::atomic_bool _startUpComplete;
std::atomic_bool _shutdownQueued;
bool _disposing = false;
std::thread _signalHandlerThread;
std::atomic_bool _stopMain{false};

void terminateProgram(int signalNumber, bool force)
{
    try
    {
        if(!force)
        {
            _shuttingDownMutex.lock();
            if(!_startUpComplete)
            {
                Gd::out.printMessage("Info: Startup is not complete yet. Queueing shutdown.");
                _shutdownQueued = true;
                _shuttingDownMutex.unlock();
                return;
            }
            if(Gd::bl->shuttingDown)
            {
                _shuttingDownMutex.unlock();
                return;
            }
        }

        Gd::out.printMessage("(Shutdown) => Stopping Homegear Gateway (Signal: " + std::to_string(signalNumber) + ")");
        Gd::bl->shuttingDown = true;
        _shuttingDownMutex.unlock();
        _disposing = true;
        if(Gd::upnp)
        {
            Gd::out.printInfo("Stopping UPnP server...");
            Gd::upnp->stop();
        }
        Gd::rpcServer->stop();
        Gd::rpcServer.reset();

        Gd::out.printMessage("(Shutdown) => Shutdown complete.");
        fclose(stdout);
        fclose(stderr);
        gnutls_global_deinit();
        gcry_control(GCRYCTL_SUSPEND_SECMEM_WARN);
        gcry_control(GCRYCTL_TERM_SECMEM);
        gcry_control(GCRYCTL_RESUME_SECMEM_WARN);

        _stopMain = true;
        return;
    }
    catch(const std::exception& ex)
    {
        Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    _exit(1);
}

void signalHandlerThread()
{
    sigset_t set{};
    int signalNumber = -1;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGABRT);
    sigaddset(&set, SIGSEGV);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGILL);
    sigaddset(&set, SIGFPE);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);
    sigaddset(&set, SIGTSTP);
    sigaddset(&set, SIGTTIN);
    sigaddset(&set, SIGTTOU);

    while(true)
    {
        try
        {
            sigwait(&set, &signalNumber);
            if(signalNumber == SIGTERM || signalNumber == SIGINT)
            {
                terminateProgram(signalNumber, false);
                return;
            }
            else if(signalNumber == SIGHUP)
            {
                Gd::out.printMessage("Info: SIGHUP received...");
                _shuttingDownMutex.lock();
                Gd::out.printMessage("Info: Reloading...");
                if(!_startUpComplete)
                {
                    _shuttingDownMutex.unlock();
                    Gd::out.printError("Error: Cannot reload. Startup is not completed.");
                    return;
                }
                _startUpComplete = false;
                _shuttingDownMutex.unlock();
                if(!std::freopen((Gd::settings.logFilePath() + "homegear-gateway.log").c_str(), "a", stdout))
                {
                    Gd::out.printError("Error: Could not redirect output to new log file.");
                }
                if(!std::freopen((Gd::settings.logFilePath() + "homegear-gateway.err").c_str(), "a", stderr))
                {
                    Gd::out.printError("Error: Could not redirect errors to new log file.");
                }
                _shuttingDownMutex.lock();
                _startUpComplete = true;
                if(_shutdownQueued)
                {
                    _shuttingDownMutex.unlock();
                    terminateProgram(SIGTERM, false);
                    return;
                }
                _shuttingDownMutex.unlock();
                Gd::out.printInfo("Info: Reload complete.");
            }
            else if(signalNumber == SIGUSR1 && _stopMain) return;
            else
            {
                if(!_disposing) Gd::out.printCritical("Critical: Signal " + std::to_string(signalNumber) + " received. Stopping Homegear Gateway...");
                if(signalNumber != SIGABRT && signalNumber != SIGSEGV)
                {
                    terminateProgram(signalNumber, false);
                    return;
                }
                signal(signalNumber, SIG_DFL); //Reset signal handler for the current signal to default
                kill(getpid(), signalNumber); //Generate core dump
            }
        }
        catch(const std::exception& ex)
        {
            Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }
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
	Gd::workingDirectory = std::string(path);
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
	if((unsigned)length > sizeof(path) - 1)
	{
		std::cerr << "The path the homegear binary is in has more than " + std::to_string(sizeof(path) - 1) + " characters." << std::endl;
		exit(1);
	}
	path[length] = '\0';
	Gd::executablePath = std::string(path);
	Gd::executablePath = Gd::executablePath.substr(0, Gd::executablePath.find_last_of("/") + 1);
#endif

	Gd::executableFile = std::string(argc > 0 ? argv[0] : "homegear");
	BaseLib::HelperFunctions::trim(Gd::executableFile);
	if(Gd::executableFile.empty()) Gd::executableFile = "homegear";
	std::pair<std::string, std::string> pathNamePair = BaseLib::HelperFunctions::splitLast(Gd::executableFile, '/');
	if(!pathNamePair.second.empty()) Gd::executableFile = pathNamePair.second;
}

void initGnuTls()
{
	// {{{ Init gcrypt and GnuTLS
		gcry_error_t gcryResult;
		if((gcryResult = gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread)) != GPG_ERR_NO_ERROR)
		{
			Gd::out.printCritical("Critical: Could not enable thread support for gcrypt.");
			exit(2);
		}

		if (!gcry_check_version(GCRYPT_VERSION))
		{
			Gd::out.printCritical("Critical: Wrong gcrypt version.");
			exit(2);
		}
		gcry_control(GCRYCTL_SUSPEND_SECMEM_WARN);
		if((gcryResult = gcry_control(GCRYCTL_INIT_SECMEM, (int)Gd::settings.secureMemorySize(), 0)) != GPG_ERR_NO_ERROR)
		{
			Gd::out.printCritical("Critical: Could not allocate secure memory. Error code is: " + std::to_string((int32_t)gcryResult));
			exit(2);
		}
		gcry_control(GCRYCTL_RESUME_SECMEM_WARN);
		gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

		int32_t gnutlsResult = 0;
		if((gnutlsResult = gnutls_global_init()) != GNUTLS_E_SUCCESS)
		{
			Gd::out.printCritical("Critical: Could not initialize GnuTLS: " + std::string(gnutls_strerror(gnutlsResult)));
			exit(2);
		}
	// }}}
}

void setLimits()
{
	struct rlimit limits;
	if(!Gd::settings.enableCoreDumps()) prctl(PR_SET_DUMPABLE, 0);
	else
	{
		//Set rlimit for core dumps
		getrlimit(RLIMIT_CORE, &limits);
		limits.rlim_cur = limits.rlim_max;
		Gd::out.printInfo("Info: Setting allowed core file size to \"" + std::to_string(limits.rlim_cur) + "\" for user with id " + std::to_string(getuid()) + " and group with id " + std::to_string(getgid()) + '.');
		setrlimit(RLIMIT_CORE, &limits);
		getrlimit(RLIMIT_CORE, &limits);
		Gd::out.printInfo("Info: Core file size now is \"" + std::to_string(limits.rlim_cur) + "\".");
	}
}

void printHelp()
{
	std::cout << "Usage: homegear-gateway [OPTIONS]" << std::endl << std::endl;
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
			exit(1);
		}
		if(pid > 0)
		{
			exit(0);
		}

		//Set process permission
		umask(S_IWGRP | S_IWOTH);

		//Set child processe's id
		sid = setsid();
		if(sid < 0)
		{
			exit(1);
		}

		close(STDIN_FILENO);
	}
	catch(const std::exception& ex)
    {
    	Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void startUp()
{
	try
	{
		if((chdir(Gd::settings.workingDirectory().c_str())) < 0)
		{
			Gd::out.printError("Could not change working directory to " + Gd::settings.workingDirectory() + ".");
			exit(1);
		}

        {
            sigset_t set{};
            sigemptyset(&set);
            sigaddset(&set, SIGHUP);
            sigaddset(&set, SIGTERM);
            sigaddset(&set, SIGINT);
            sigaddset(&set, SIGABRT);
            sigaddset(&set, SIGSEGV);
            sigaddset(&set, SIGQUIT);
            sigaddset(&set, SIGILL);
            sigaddset(&set, SIGFPE);
            sigaddset(&set, SIGALRM);
            sigaddset(&set, SIGUSR1);
            sigaddset(&set, SIGUSR2);
            sigaddset(&set, SIGTSTP);
            sigaddset(&set, SIGTTIN);
            sigaddset(&set, SIGTTOU);
            sigprocmask(SIG_BLOCK, &set, nullptr);
        }

    	if(Gd::settings.memoryDebugging()) mallopt(M_CHECK_ACTION, 3); //Print detailed error message, stack trace, and memory, and abort the program. See: http://man7.org/linux/man-pages/man3/mallopt.3.html

    	initGnuTls();

		setLimits();

        Gd::bl->threadManager.start(_signalHandlerThread, true, &signalHandlerThread);

        if(!std::freopen((Gd::settings.logFilePath() + "homegear-gateway.log").c_str(), "a", stdout))
        {
            Gd::out.printError("Error: Could not redirect output to log file.");
        }
        if(!std::freopen((Gd::settings.logFilePath() + "homegear-gateway.err").c_str(), "a", stderr))
        {
            Gd::out.printError("Error: Could not redirect errors to log file.");
        }

        Gd::out.printMessage("Starting Homegear Gateway...");

        if(Gd::runAsUser.empty()) Gd::runAsUser = Gd::settings.runAsUser();
        if(Gd::runAsGroup.empty()) Gd::runAsGroup = Gd::settings.runAsGroup();
        if((!Gd::runAsUser.empty() && Gd::runAsGroup.empty()) || (!Gd::runAsGroup.empty() && Gd::runAsUser.empty()))
        {
            Gd::out.printCritical("Critical: You only provided a user OR a group for Homegear to run as. Please specify both.");
            terminateProgram(SIGTERM, true);
            return;
        }
        uid_t userId = Gd::bl->hf.userId(Gd::runAsUser);
        gid_t groupId = Gd::bl->hf.groupId(Gd::runAsGroup);
        std::string currentPath;
        if(!Gd::pidfilePath.empty() && Gd::pidfilePath.find('/') != std::string::npos)
        {
            currentPath = Gd::pidfilePath.substr(0, Gd::pidfilePath.find_last_of('/'));
            if(!currentPath.empty())
            {
                if(!BaseLib::Io::directoryExists(currentPath)) BaseLib::Io::createDirectory(currentPath, S_IRWXU | S_IRWXG);
                if(chown(currentPath.c_str(), userId, groupId) == -1) std::cerr << "Could not set owner on " << currentPath << std::endl;
                if(chmod(currentPath.c_str(), S_IRWXU | S_IRWXG) == -1) std::cerr << "Could not set permissions on " << currentPath << std::endl;
            }
        }

		//{{{ Export GPIOs
		if(getuid() == 0 && (Gd::settings.gpio1() != -1 || Gd::settings.gpio2() != -1))
		{
			std::vector<uint32_t> gpios;
			gpios.reserve(2);
			if(Gd::settings.gpio1() != -1) gpios.push_back(Gd::settings.gpio1());
			if(Gd::settings.gpio2() != -1) gpios.push_back(Gd::settings.gpio2());
			if(!gpios.empty())
			{
				BaseLib::LowLevel::Gpio gpio(Gd::bl.get(), Gd::settings.gpioPath());
				if(Gd::bl->userId != 0 && Gd::bl->groupId != 0) gpio.setup(Gd::bl->userId, Gd::bl->groupId, true, gpios);
				else gpio.setup(0, 0, false, gpios);
			}
		}
		//}}}

    	if(getuid() == 0 && !Gd::runAsUser.empty() && !Gd::runAsGroup.empty())
    	{
			if(Gd::bl->userId == 0 || Gd::bl->groupId == 0)
			{
				Gd::out.printCritical("Could not drop privileges. User name or group name is not valid.");
                terminateProgram(SIGTERM, true);
                return;
			}

			Gd::out.printInfo("Info: Dropping privileges to user " + Gd::runAsUser + " (" + std::to_string(Gd::bl->userId) + ") and group " + Gd::runAsGroup + " (" + std::to_string(Gd::bl->groupId) + ")");

			int result = -1;
			std::vector<gid_t> supplementaryGroups(10);
			int numberOfGroups = 10;
			while(result == -1)
			{
				result = getgrouplist(Gd::runAsUser.c_str(), 10000, supplementaryGroups.data(), &numberOfGroups);

				if(result == -1) supplementaryGroups.resize(numberOfGroups);
				else supplementaryGroups.resize(result);
			}

			if(setgid(Gd::bl->groupId) != 0)
			{
				Gd::out.printCritical("Critical: Could not drop group privileges.");
                terminateProgram(SIGTERM, true);
                return;
			}

			if(setgroups(supplementaryGroups.size(), supplementaryGroups.data()) != 0)
			{
				Gd::out.printCritical("Critical: Could not set supplementary groups: " + std::string(strerror(errno)));
                terminateProgram(SIGTERM, true);
                return;
			}

			if(setuid(Gd::bl->userId) != 0)
			{
				Gd::out.printCritical("Critical: Could not drop user privileges.");
                terminateProgram(SIGTERM, true);
                return;
			}

			//Core dumps are disabled by setuid. Enable them again.
			if(Gd::settings.enableCoreDumps()) prctl(PR_SET_DUMPABLE, 1);
    	}

    	if(getuid() == 0)
    	{
    		if(!Gd::runAsUser.empty() && !Gd::runAsGroup.empty())
    		{
    			Gd::out.printCritical("Critical: Homegear still has root privileges though privileges should have been dropped. Exiting Homegear as this is a security risk.");
                terminateProgram(SIGTERM, true);
                return;
    		}
    		else Gd::out.printWarning("Warning: Running as root. The authors of Homegear recommend running Homegear as user.");
    	}
    	else
    	{
    		if(setuid(0) != -1)
			{
				Gd::out.printCritical("Critical: Regaining root privileges succeded. Exiting Homegear as this is a security risk.");
                terminateProgram(SIGTERM, true);
                return;
			}
    		Gd::out.printInfo("Info: Homegear Gateway is (now) running as user with id " + std::to_string(getuid()) + " and group with id " + std::to_string(getgid()) + '.');
    	}

    	//Create PID file
    	try
    	{
			if(!Gd::pidfilePath.empty())
			{
				int32_t pidfile = open(Gd::pidfilePath.c_str(), O_CREAT | O_RDWR, 0666);
				if(pidfile < 0)
				{
					Gd::out.printError("Error: Cannot create pid file \"" + Gd::pidfilePath + "\".");
				}
				else
				{
					int32_t rc = flock(pidfile, LOCK_EX | LOCK_NB);
					if(rc && errno == EWOULDBLOCK)
					{
						Gd::out.printError("Error: Homegear Gateway is already running - Can't lock PID file.");
					}
					std::string pid(std::to_string(getpid()));
					int32_t bytesWritten = write(pidfile, pid.c_str(), pid.size());
					if(bytesWritten <= 0) Gd::out.printError("Error writing to PID file: " + std::string(strerror(errno)));
					close(pidfile);
				}
			}
		}
		catch(const std::exception& ex)
		{
			Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
		}

        if(Gd::settings.waitForCorrectTime())
        {
            while(BaseLib::HelperFunctions::getTime() < 1000000000000)
            {
                Gd::out.printWarning("Warning: Time is in the past. Waiting for ntp to set the time...");
                std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            }
        }

        if(!Gd::settings.waitForIp4OnInterface().empty())
        {
            std::string ipAddress;
            while(ipAddress.empty())
            {
                try
                {
                    ipAddress = BaseLib::Net::getMyIpAddress(Gd::settings.waitForIp4OnInterface());
                }
                catch(const BaseLib::NetException& ex)
                {
                    Gd::out.printDebug("Debug: " + std::string(ex.what()));
                }
                if(_shutdownQueued)
                {
                    terminateProgram(SIGTERM, true);
                    return;
                }
                if(ipAddress.empty())
                {
                    Gd::out.printWarning("Warning: " + Gd::settings.waitForIp4OnInterface() + " has no IPv4 address assigned yet. Waiting...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                }
            }
        }

        if(!Gd::settings.waitForIp6OnInterface().empty())
        {
            std::string ipAddress;
            while(ipAddress.empty())
            {
                try
                {
                    ipAddress = BaseLib::Net::getMyIp6Address(Gd::settings.waitForIp6OnInterface());
                }
                catch(const BaseLib::NetException& ex)
                {
                    Gd::out.printDebug("Debug: " + std::string(ex.what()));
                }
                if(_shutdownQueued)
                {
                    terminateProgram(SIGTERM, true);
                    return;
                }
                if(ipAddress.empty())
                {
                    Gd::out.printWarning("Warning: " + Gd::settings.waitForIp6OnInterface() + " has no IPv6 address assigned yet. Waiting...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                }
            }
        }

		Gd::rpcServer.reset(new RpcServer(Gd::bl.get()));
		if(!_shutdownQueued)
        {
            if(!Gd::rpcServer->start())
            {
                Gd::out.printCritical("Critical: Could not start.");
                _shutdownQueued = true;
            }
        }

        Gd::out.printMessage("Startup complete.");

		if(Gd::settings.enableUpnp())
		{
			Gd::out.printInfo("Starting UPnP server...");
			Gd::upnp = std::unique_ptr<UPnP>(new UPnP());
			Gd::upnp->start();
		}

        Gd::bl->booting = false;

        _shuttingDownMutex.lock();
		_startUpComplete = true;
		if(_shutdownQueued)
		{
			_shuttingDownMutex.unlock();
            terminateProgram(SIGTERM, false);
            return;
		}
		_shuttingDownMutex.unlock();

		if(BaseLib::Io::fileExists(Gd::settings.workingDirectory() + "core"))
		{
			Gd::out.printError("Error: A core file exists in Homegear Gateway's working directory (\"" + Gd::settings.workingDirectory() + "core" + "\"). Please send this file to the Homegear team including information about your system (Linux distribution, CPU architecture), the Homegear Gateway version, the current log files and information what might've caused the error.");
		}

        if(_txTestMode)
        {
            Gd::rpcServer->txTest();
        }

       	while(!_stopMain) std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	catch(const std::exception& ex)
    {
    	Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

int main(int argc, char* argv[])
{
	try
    {
		_startUpComplete = false;
		_shutdownQueued = false;

    	getExecutablePath(argc, argv);
    	Gd::bl.reset(new BaseLib::SharedObjects());
    	Gd::out.init(Gd::bl.get());

		if(BaseLib::Io::directoryExists(Gd::executablePath + "config")) Gd::configPath = Gd::executablePath + "config/";
		else if(BaseLib::Io::directoryExists(Gd::executablePath + "cfg")) Gd::configPath = Gd::executablePath + "cfg/";
		else Gd::configPath = "/etc/homegear/";

    	if(std::string(VERSION) != Gd::bl->version())
    	{
    		Gd::out.printCritical(std::string("Base library has wrong version. Expected version ") + VERSION + " but got version " + Gd::bl->version());
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
    				if(!configPath.empty()) Gd::configPath = configPath;
    				if(Gd::configPath[Gd::configPath.size() - 1] != '/') Gd::configPath.push_back('/');
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
    				Gd::pidfilePath = std::string(argv[i + 1]);
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
    				Gd::runAsUser = std::string(argv[i + 1]);
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
    				Gd::runAsGroup = std::string(argv[i + 1]);
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
            else if(arg == "--txtest")
            {
                _txTestMode = true;
            }
    		else if(arg == "-v")
    		{
    			std::cout << "Homegear Gateway version " << VERSION << std::endl;
    			std::cout << "Copyright (c) 2013-2019 Homegear GmbH" << std::endl << std::endl;
    			exit(0);
    		}
    		else
    		{
    			printHelp();
    			exit(1);
    		}
    	}

    	// {{{ Load settings
			Gd::out.printInfo("Loading settings from " + Gd::configPath + "gateway.conf");
			Gd::settings.load(Gd::configPath + "gateway.conf", Gd::executablePath);
			if(Gd::runAsUser.empty()) Gd::runAsUser = Gd::settings.runAsUser();
			if(Gd::runAsGroup.empty()) Gd::runAsGroup = Gd::settings.runAsGroup();
			if((!Gd::runAsUser.empty() && Gd::runAsGroup.empty()) || (!Gd::runAsGroup.empty() && Gd::runAsUser.empty()))
			{
				Gd::out.printCritical("Critical: You only provided a user OR a group for Homegear Gateway to run as. Please specify both.");
				exit(1);
			}
			Gd::bl->userId = Gd::bl->hf.userId(Gd::runAsUser);
			Gd::bl->groupId = Gd::bl->hf.groupId(Gd::runAsGroup);
			if((int32_t)Gd::bl->userId == -1 || (int32_t)Gd::bl->groupId == -1)
			{
				Gd::bl->userId = 0;
				Gd::bl->groupId = 0;
			}
		// }}}

		if((chdir(Gd::settings.workingDirectory().c_str())) < 0)
		{
			Gd::out.printError("Could not change working directory to " + Gd::settings.workingDirectory() + ".");
            exit(1);
		}

		if(_startAsDaemon) startDaemon();
    	startUp();

        kill(getpid(), SIGUSR1);
        Gd::bl->threadManager.join(_signalHandlerThread);
        return 0;
    }
    catch(const std::exception& ex)
	{
		Gd::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
    _exit(1);
}
