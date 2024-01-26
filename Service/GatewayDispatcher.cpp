#include "pch.h"
#include "GatewayDispatcher.h"


//////////////////////////////////////////////////////////////////////
// class GatewayDispatcher
//

bool GatewayDispatcher::start(const String &connectorString)
{
	if (connectorString)
	{
		return true;
	}

	if (!NetServer::start())
	{
		return false;
	}

	m_connectorString = connectorString;
	return startListener(m_connectorString);
}


void GatewayDispatcher::stop(unsigned timeout)
{
	NetServer::stop(timeout);
	m_hostMap = nullptr;
}


NetContext *GatewayDispatcher::createContext()
{
	return new GatewayContext(this);
}


GatewayHostPtr GatewayDispatcher::lookupHost(const char *hostName)
{
	SyncSharedLock lock(m_hostMutex);
	return m_hostMap->lookup(hostName);
}
