#pragma once

#include "GatewayProvider.h"


//////////////////////////////////////////////////////////////////////////
// class GatewayHost
//

class GatewayHost : public RefCounter
{
public:
	GatewayHost();
	virtual ~GatewayHost();

	size_t getProviderCount() const;

	GatewayProviderPtr lookupProvider(HttpUri &uri) const;

	void addProvider(const char *path, GatewayProvider *provider);

private:
	HttpFolderIndex<GatewayProviderPtr> m_providers;
};

typedef RefPointer<GatewayHost> GatewayHostPtr;


//////////////////////////////////////////////////////////////////////////
// class GatewayHostMap
//

class GatewayHostMap : public RefCounter
{
public:
	GatewayHostPtr get(const char *hostName);
	void set(String hostName, GatewayHost *host);

private:
	SyncMutex m_hostMutex;
	PathIndex<GatewayHostPtr> m_hosts;
};

typedef RefPointer<GatewayHostMap> GatewayHostMapPtr;



/*
* Inline Implementations
*/

inline GatewayHost::GatewayHost()
{
}

inline GatewayHost::~GatewayHost()
{
}

inline size_t GatewayHost::getProviderCount() const
{
	return m_providers.getCount();
}

inline GatewayProviderPtr GatewayHost::lookupProvider(HttpUri &uri) const
{
	GatewayProviderPtr provider;

	size_t pathInfoPos = 0;
	String path = uri.getPath();
	if (m_providers.lookupFolder(path, provider, pathInfoPos))
	{
		uri.setPathInfoPos(pathInfoPos);
	}
	else
	{
		provider = nullptr;
	}

	return provider;
}

inline void GatewayHost::addProvider(const char *path, GatewayProvider *provider)
{
	m_providers.set(path, provider);
}



inline GatewayHostPtr GatewayHostMap::get(const char *hostName)
{
	GatewayHostPtr host;

	m_hostMutex.lockShared();
	if (!m_hosts.get(hostName, host))
	{
		m_hostMutex.unlockShared();
		m_hostMutex.lock();

		if (!m_hosts.get(hostName, host))
		{
			// Detect wildcard.
			String wildcard = hostName;
			wildcard.reverse();

			size_t endPos = -1;
			if (!m_hosts.get(wildcard, host, &endPos)
				|| ((endPos != -1)
					&& (endPos != wildcard.getLength())
					&& (wildcard[endPos] != '.')))
			{
				host = nullptr;
			}

			m_hosts.set(hostName, host);
		}

		m_hostMutex.unlock();
	}
	else
	{
		m_hostMutex.unlockShared();
	}

	return host;
}

inline void GatewayHostMap::set(String hostName, GatewayHost *host)
{
	if (!hostName.trim().isEmpty())
	{
		// Handle wildcard.
		if (hostName[0] == '*')
		{
			hostName.trimLeft('*');
			hostName.trimLeft('.').reverse();
		}

		SyncLock lock(m_hostMutex);
		m_hosts.set(hostName, host);
	}
}
