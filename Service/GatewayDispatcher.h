#pragma once
#include "GatewayContext.h"


//////////////////////////////////////////////////////////////////////
// class GatewayDispatcher
//

class GatewayDispatcher : public NetServer, public RefCounter
{
public:
	GatewayDispatcher();

	virtual bool start(const String &connectorString);
	virtual void stop(unsigned timeout = 5000);

	String getConnectorString() const;

	void setHostMap(GatewayHostMap *hostMap);
	GatewayHostPtr lookupHost(const char *hostName);

	using NetServer::endContext;

protected:
	virtual NetContext *createContext();

private:
	SyncMutex m_hostMutex;
	String m_connectorString;
	GatewayHostMapPtr m_hostMap;
};

using GatewayDispatcherPtr = RefPointer<GatewayDispatcher>;
using GatewayDispatcherMap = std::unordered_map<String, GatewayDispatcherPtr>;


inline GatewayDispatcher::GatewayDispatcher()
{
}

inline String GatewayDispatcher::getConnectorString() const
{
	return m_connectorString;
}

inline void GatewayDispatcher::setHostMap(GatewayHostMap *hostMap)
{
	m_hostMutex.lock();
	m_hostMap = hostMap;
	m_hostMutex.unlock();
}

inline GatewayHostPtr GatewayDispatcher::lookupHost(const char *hostName)
{
	SyncSharedLock lock(m_hostMutex);
	return m_hostMap->get(hostName);
}
