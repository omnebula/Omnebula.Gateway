#include "pch.h"
#include <AfxCore/NetTls.h>
#include "GatewayService.h"


static const String SERVICE_CONFIG_FILENAME = "service.xml";
static const String HOST_CONFIG_FILENAME = "hosts.xml";


//////////////////////////////////////////////////////////////////////
// class OmnebulaGatewayServiceApp
//

OmnebulaGatewayServiceApp theApp;


OmnebulaGatewayServiceApp::OmnebulaGatewayServiceApp() :
	ServiceApp("Omnebula.Gateway")
{
}


bool OmnebulaGatewayServiceApp::initApp()
{
	if (!__super::initApp())
	{
		return false;
	}

	if (!Http::Init())
	{
		return false;
	}

	if (!initConfigs())
	{
		return false;
	}

	return true;
}

bool OmnebulaGatewayServiceApp::initConfigs()
{
	if (!m_configMonitor.addFile(SERVICE_CONFIG_FILENAME, [this](Xml &serviceConfig) mutable { return loadServiceConfig(serviceConfig); })
		|| !m_configMonitor.addFile(HOST_CONFIG_FILENAME, [this](Xml &hostConfig) mutable { return loadHostConfig(hostConfig); }))
	{
		return false;
	}

	if (!m_configMonitor.start())
	{
		AfxLogLastError("OmnebulaGatewayServiceApp::initHosts@StartMonitor");
		return false;
	}

	return true;
}

bool OmnebulaGatewayServiceApp::loadServiceConfig(Xml &serviceConfig)
{
	AfxLogInfo("Loading service configuration");

	// Close existing containers.
	NetTlsProtocol::CloseAllContainers();

	// Open the service's cert store.
	String containerName;
	ulong_t containerType;

	// If we're running as a service, use qualified store name.
	if (AfxIsServiceSession())
	{
		containerName = getAppName() + "\\My";
		containerType = CERT_SYSTEM_STORE_SERVICES;
	}
	// Otherwise, use current user's cert store.
	else
	{
		containerName = "My";
		containerType = CERT_SYSTEM_STORE_CURRENT_USER;
	}

	if (NetTlsProtocol::OpenCertContainer(containerName, containerType | CERT_STORE_READONLY_FLAG))
	{
		AfxLogInfo("Opened certificate store '%s'", getAppName());
	}
	else
	{
		AfxLogLastError("OmnebulaGatewayServiceApp::loadServiceConfig@OpenCertContainer(%s)", containerName);
		return false;
	}

	// Check for custom cert stores. Must be stored in local machine.
	Xml &certs = serviceConfig["certs"];
	for (auto &current : certs)
	{
		containerName = current.getData();
		if (!containerName.isEmpty())
		{
			if (NetTlsProtocol::OpenMachineContainer(containerName))
			{
				AfxLogInfo("Opened certificate store '%s'", containerName);
			}
			else
			{
				AfxLogWarning("OmnebulaProxyApp::loadServiceConfig@OpenCustomMachineContainer(%s) - %s", containerName, AfxFormatLastError());
			}
		}
	}

	AfxLogInfo("Successfully loaded service configuration");

	return true;
}

bool OmnebulaGatewayServiceApp::loadHostConfig(Xml &hostsConfig)
{
	AfxLogInfo("Loading host configuration");

	GatewayHostConfig hostConfig;
	if (!hostConfig.load(hostsConfig))
	{
		return false;
	}

	// Update dispatcher map.
	std::vector<GatewayDispatcherPtr> droppedDispatchers;

	SyncLock lock(m_dispatcherMutex);

	for (auto it : m_dispatcherMap)
	{
		const String &connectorString = it.first;
		GatewayDispatcherPtr dispatcher = it.second;

		GatewayHostMapPtr hostMap = hostConfig.popHostMap(connectorString);
		if (hostMap)
		{
			dispatcher->setHostMap(hostMap);
		}
		else
		{
			droppedDispatchers.push_back(dispatcher);
		}
	}

	// Start
	hostConfig.forEachHostMap(
		[this](const String &connectorString, GatewayHostMap *hostMap) mutable
		{
			GatewayDispatcherPtr dispatcher = new GatewayDispatcher;

			dispatcher->setHostMap(hostMap);

			if (dispatcher->start(connectorString))
			{
				m_dispatcherMap[connectorString] = dispatcher;
			}
		}
	);

	// Discard dropped dispatchers.
	for (auto dispatcher : droppedDispatchers)
	{
		dispatcher->stop();
		m_dispatcherMap.erase(dispatcher->getConnectorString());
	}

	AfxLogInfo("Successfully loaded host configuration");

	return true;
}


void OmnebulaGatewayServiceApp::exitApp()
{
	SyncLock lock(m_dispatcherMutex);
	for (auto &it : m_dispatcherMap)
	{
		it.second->stop();
	}

	m_configMonitor.stop();

	ServiceApp::exitApp();
}
