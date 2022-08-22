#pragma once


class GatewayContext;
class GatewayResponseContext;
class GatewayDispatcher;


//////////////////////////////////////////////////////////////////////////
// class GatewayProvider
//

class GatewayProvider : public RefCounter
{
public:
	GatewayProvider(const Xml &config, const String &target);

	String getUri() const;
	String splitVirtualPath(const String &urlPath) const;

	String getTarget() const;

	void beginDispatch(GatewayContext *context, const HttpUri &uri);

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri) = 0;

protected:
	String m_uri;
	String m_target;

	String m_basicAuthRealm;
	PropertyMap m_basicAuthUsers;

protected:
	void syncConnectionType(HttpRequest &request, HttpResponse &response);
};

using GatewayProviderPtr = RefPointer<GatewayProvider>;


//////////////////////////////////////////////////////////////////////////
// class GatewayRedirectProvider
//

class GatewayRedirectProvider : public GatewayProvider
{
public:
	GatewayRedirectProvider(const Xml &config, const String &target);

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

private:
	String m_newScheme;
	String m_newHost;
	String m_newPath;
	String m_newQuery;
};

using GatewayRedirectProviderPtr = RefPointer<GatewayRedirectProvider>;


//////////////////////////////////////////////////////////////////////////
// class GatewayFileProvider
//

class GatewayFileProvider : public GatewayProvider
{
public:
	GatewayFileProvider(const Xml &config, const String &target);

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

private:
	String m_defaultFile;
	String m_defaultExtension;
};

using GatewayFileProviderPtr = RefPointer<GatewayFileProvider>;


//////////////////////////////////////////////////////////////////////////
// class GatewayServerProvider
//

class GatewayServerProvider : public GatewayProvider
{
public:
	GatewayServerProvider(const Xml &config, const String &target);
	virtual ~GatewayServerProvider();

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

private:
	/* Options */
	String m_newHost;
	String m_newPath;
	String m_newQuery;

	/* Connection Pooling */
	struct ConnectionPool : public NetConnectionPool, public RefCounter
	{
		std::atomic<long> m_acquisitionCount;

		ConnectionPool();
	};
	using ConnectionPoolPtr = RefPointer<ConnectionPool>;

	static SyncMutex sm_connectionPoolMapMutex;
	static std::unordered_map<String, ConnectionPoolPtr> sm_connectionPoolMap;

	static ConnectionPool *AcquireConnectionPool(const String &connector);
	static void ReleaseConnectionPool(const String &connector);

	ConnectionPool *m_connectionPool;
};

using GatewayServerProviderPtr = RefPointer<GatewayServerProvider>;



/*
* Inline Implementations
*/

inline String GatewayProvider::getUri() const
{
	return m_uri;
}

inline String GatewayProvider::splitVirtualPath(
	const String &sourcePath) const
{
	String virtualPath;

	size_t offset = m_uri.getLength();
	if (offset < sourcePath.getLength())
	{
		if (sourcePath[offset] == '/')
		{
			offset++;
		}

		virtualPath = sourcePath.mid(offset);
	}

	return virtualPath;
}

inline String GatewayProvider::getTarget() const
{
	return m_target;
}

inline void GatewayProvider::syncConnectionType(HttpRequest &request, HttpResponse &response)
{
	String type = response.getHeader(HttpHeader::CONNECTION);
	if (type.isEmpty())
	{
		type = request.getHeader(HttpHeader::CONNECTION);
		if (type.isEmpty())
		{
			type = HttpConfig.DefaultConnectionType;
		}

		response.setHeader(HttpHeader::CONNECTION, type);
	}
}



inline GatewayServerProvider::ConnectionPool::ConnectionPool() :
	m_acquisitionCount(0)
{
}