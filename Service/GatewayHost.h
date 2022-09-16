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
	GatewayHostPtr lookup(const char *hostName);
	void insert(String hostName, GatewayHost *host);

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

	if (!m_providers.lookup(uri, provider))
	{
		provider = nullptr;
	}

	return provider;
}

inline void GatewayHost::addProvider(const char *path, GatewayProvider *provider)
{
	m_providers.insert(path, provider);
}



inline GatewayHostPtr GatewayHostMap::lookup(const char *hostName)
{
	GatewayHostPtr host;

	m_hostMutex.lockShared();
	if (!m_hosts.lookup(hostName, host))
	{
		m_hostMutex.unlockShared();
		m_hostMutex.lock();

		if (!m_hosts.lookup(hostName, host))
		{
			// Detect wildcard.
			String wildcard = hostName;
			wildcard.reverse();

			size_t endPos = -1;
			if (!m_hosts.lookup(wildcard, host, &endPos)
				|| ((endPos != -1)
					&& (endPos != wildcard.getLength())
					&& (wildcard[endPos] != '.')))
			{
				host = nullptr;
			}

			m_hosts.insert(hostName, host);
		}

		m_hostMutex.unlock();
	}
	else
	{
		m_hostMutex.unlockShared();
	}

	return host;
}

inline void GatewayHostMap::insert(String hostName, GatewayHost *host)
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
		m_hosts.insert(hostName, host);
	}
}
