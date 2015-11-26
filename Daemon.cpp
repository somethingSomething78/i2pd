#include <thread>
#include <memory>

#include "Daemon.h"

#include "Log.h"
#include "Base.h"
#include "version.h"
#include "Transports.h"
#include "NTCPSession.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "Tunnel.h"
#include "NetDb.h"
#include "Garlic.h"
#include "util.h"
#include "Streaming.h"
#include "Destination.h"
#include "HTTPServer.h"
#include "I2PControl.h"
#include "ClientContext.h"
// ssl.h somehow pulls Windows.h stuff that has to go after asio
#include <openssl/ssl.h>

#ifdef USE_UPNP
#include "UPnP.h"
#endif

namespace i2p
{
	namespace util
	{
		class Daemon_Singleton::Daemon_Singleton_Private
		{
		public:
			Daemon_Singleton_Private() {};
			~Daemon_Singleton_Private() {};

			std::unique_ptr<i2p::util::HTTPServer> httpServer;
			std::unique_ptr<i2p::client::I2PControlService> m_I2PControlService;

#ifdef USE_UPNP
			i2p::transport::UPnP m_UPnP;
#endif	
		};

		Daemon_Singleton::Daemon_Singleton() : running(1), d(*new Daemon_Singleton_Private()) {};
		Daemon_Singleton::~Daemon_Singleton() {
			delete &d;
		};

		bool Daemon_Singleton::IsService () const
		{
#ifndef _WIN32
			return i2p::util::config::GetArg("-service", 0);
#else
			return false;
#endif
		}

		bool Daemon_Singleton::init(int argc, char* argv[])
		{
			SSL_library_init ();
			i2p::util::config::OptionParser(argc, argv);
			i2p::context.Init ();

			LogPrint("\n\n\n\ni2pd starting\n");
			LogPrint("Version ", VERSION);
			LogPrint("data directory: ", i2p::util::filesystem::GetDataDir().string());
			i2p::util::filesystem::ReadConfigFile(i2p::util::config::mapArgs, i2p::util::config::mapMultiArgs);

			isDaemon = i2p::util::config::GetArg("-daemon", 0);
			isLogging = i2p::util::config::GetArg("-log", 1);

			int port = i2p::util::config::GetArg("-port", 0);
			if (port)
				i2p::context.UpdatePort (port);					
			const char * host = i2p::util::config::GetCharArg("-host", "");
			if (host && host[0])
				i2p::context.UpdateAddress (boost::asio::ip::address::from_string (host));	

			i2p::context.SetSupportsV6 (i2p::util::config::GetArg("-v6", 0));
			i2p::context.SetFloodfill (i2p::util::config::GetArg("-floodfill", 0));
			auto bandwidth = i2p::util::config::GetArg("-bandwidth", "");
			if (bandwidth.length () > 0)
			{
				if (bandwidth[0] > 'L')
					i2p::context.SetHighBandwidth ();
				else
					i2p::context.SetLowBandwidth ();
			}	

			LogPrint("CMD parameters:");
			for (int i = 0; i < argc; ++i)
				LogPrint(i, "  ", argv[i]);

			return true;
		}
			
		bool Daemon_Singleton::start()
		{
			// initialize log			
			if (isLogging)
			{
				if (isDaemon)
				{
					std::string logfile_path = IsService () ? "/var/log" : i2p::util::filesystem::GetDataDir().string();
#ifndef _WIN32
					logfile_path.append("/i2pd.log");
#else
					logfile_path.append("\\i2pd.log");
#endif
					StartLog (logfile_path);
				}
				else
					StartLog (""); // write to stdout
			}

			d.httpServer = std::unique_ptr<i2p::util::HTTPServer>(new i2p::util::HTTPServer(i2p::util::config::GetArg("-httpport", 7070)));
			d.httpServer->Start();
			LogPrint("HTTP Server started");
			i2p::data::netdb.Start();
			LogPrint("NetDB started");
#ifdef USE_UPNP
			d.m_UPnP.Start ();
			LogPrint(eLogInfo, "UPnP started");
#endif			
			i2p::transport::transports.Start();
			LogPrint("Transports started");
			i2p::tunnel::tunnels.Start();
			LogPrint("Tunnels started");
			i2p::client::context.Start ();
			LogPrint("Client started");
			// I2P Control
			int i2pcontrolPort = i2p::util::config::GetArg("-i2pcontrolport", 0);
			if (i2pcontrolPort)
			{
				d.m_I2PControlService = std::unique_ptr<i2p::client::I2PControlService>(new i2p::client::I2PControlService (i2pcontrolPort));
				d.m_I2PControlService->Start ();
				LogPrint("I2PControl started");
			}
			return true;
		}

		bool Daemon_Singleton::stop()
		{
			LogPrint("Shutdown started.");
			i2p::client::context.Stop();
			LogPrint("Client stopped");
			i2p::tunnel::tunnels.Stop();
			LogPrint("Tunnels stopped");
#ifdef USE_UPNP
			d.m_UPnP.Stop ();
			LogPrint(eLogInfo, "UPnP stopped");
#endif			
			i2p::transport::transports.Stop();
			LogPrint("Transports stopped");
			i2p::data::netdb.Stop();
			LogPrint("NetDB stopped");
			d.httpServer->Stop();
			d.httpServer = nullptr;
			LogPrint("HTTP Server stopped");
			if (d.m_I2PControlService)
			{
				d.m_I2PControlService->Stop ();
				d.m_I2PControlService = nullptr;
				LogPrint("I2PControl stopped");	
			}	
			StopLog ();

			return true;
		}
	}
}