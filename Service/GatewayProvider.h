#pragma once


class GatewayHost;
class GatewayContext;
class GatewayResponseContext;
class GatewayDispatcher;


//////////////////////////////////////////////////////////////////////////
// class GatewayProvider
//

class GatewayProvider : public RefCounter
{
public:
	GatewayProvider(GatewayHost *host, const Xml &config, const String &target);
	virtual ~GatewayProvider() = default;

	GatewayHost *getHost() const;

	String getUri() const;
	String splitVirtualPath(const String &urlPath) const;

	String getTarget() const;

	void beginDispatch(GatewayContext *context, const HttpUri &uri);

protected:
	GatewayProvider() = default;

	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri) = 0;

protected:
	String m_uri;
	String m_target;

	String m_basicAuthRealm;
	PropertyMap m_basicAuthUsers;

	GatewayHost *m_host{ nullptr };

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
	GatewayRedirectProvider(GatewayHost *host, const Xml &config, const String &target);

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
	GatewayFileProvider(GatewayHost *host, const Xml &config, const String &target);

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

private:
	String m_defaultFile;
	String m_defaultExtension;
	PropertyMap m_responseHeaders;
};

using GatewayFileProviderPtr = RefPointer<GatewayFileProvider>;


//////////////////////////////////////////////////////////////////////////
// class GatewayServerProvider
//

class GatewayServerProvider : public GatewayProvider
{
public:
	GatewayServerProvider(GatewayHost *host, const Xml &config, const String &target, bool initConnectionPool = true);
	virtual ~GatewayServerProvider();

protected:
	GatewayServerProvider();

	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

	void sendToServer(GatewayContext *context, NetStreamPtr serverStream);

protected:
	/* Options */
	String m_newHost;
	String m_newPath;
	String m_newQuery;

	/* Connection Pooling */
	class ConnectionPool : public NetConnectionPool, public RefCounter
	{
	public:
		using Ptr = RefPointer<ConnectionPool>;

		std::atomic<long> m_acquisitionCount{ 0 };
	};
	class ConnectionPoolMap : public std::unordered_map<String, ConnectionPool::Ptr>, public RefCounter
	{
	public:
		using Ptr = RefPointer<ConnectionPoolMap>;
	};

	ConnectionPool::Ptr m_connectionPool;
	ConnectionPoolMap::Ptr m_connectionPoolMap;

	static SyncMutex sm_connectionPoolMapMutex;
	static ConnectionPoolMap *sm_connectionPoolMap;

	void initConnectionPoolMap();
	
	static ConnectionPool *AcquireConnectionPool(const String &connector, bool init = true);
	static bool ReleaseConnectionPool(const String &connector);

	virtual bool allocateConnection(GatewayContext *context, NetStreamPtr &serverStream);
	virtual void freeConnection(NetStreamPtr serverStream, ConnectionPool *pool = nullptr);
};

using GatewayServerProviderPtr = RefPointer<GatewayServerProvider>;



//////////////////////////////////////////////////////////////////////////
// class GatewayPublisherProvider
//

class GatewayPublisherProvider : public WebSocketServer, public GatewayServerProvider
{
public:
	GatewayPublisherProvider(GatewayHost *host, const Xml &config, const String &target);
	virtual ~GatewayPublisherProvider();

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri) override;

	// Allocate/free remote origin server connection.
	virtual bool allocateConnection(GatewayContext *context, NetStreamPtr &serverStream) override;
	virtual void freeConnection(NetStreamPtr serverStream, ConnectionPool *pool = nullptr) override;

	virtual void onWebSocketError(ServerContext *context, ServerContext::Error &error) override;
	virtual void onWebSocketClose(ServerContext *context) override;

private:
	SyncMutex m_mutex;
	std::queue<GatewayContext*> m_pendingConnections;
	ServerContext::Ptr m_controllerContext;
	ThreadQueue m_sendQueue{ INFINITE };

	void attachSubscriber(GatewayContext *context);

private:
	class SubscriberAcceptor : public GatewayServerProvider
	{
	public:
		SubscriberAcceptor(GatewayPublisherProvider *publisher) :
			m_publisher(publisher)
		{
		}
		virtual ~SubscriberAcceptor()
		{
		}

		virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

	private:
		GatewayPublisherProvider *m_publisher;
	};
};

using GatewayPublisherProviderPtr = RefPointer<GatewayPublisherProvider>;


//////////////////////////////////////////////////////////////////////////
// class GatewaySubscriberProvider
//

class GatewaySubscriberProvider : public GatewayServerProvider
{
public:
	GatewaySubscriberProvider(GatewayHost *host, const Xml &config, const String &target);
	virtual ~GatewaySubscriberProvider();

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri) override;

private:
	void initDispatcher();
	void initPublisher();
	void connectToPublisher();
	void sendAttachRequest();

private:
	String m_socketUrl;
	String m_attachUrl;
	WebSocketClient m_publisherSocket;
	ThreadQueue m_connectQueue;
	std::atomic_bool m_isActive{ false };

	GatewayDispatcher *m_dispatcher{ nullptr };
};

using GatewaySubscriberProviderPtr = RefPointer<GatewaySubscriberProvider>;



/*
* Inline Implementations
*/
inline GatewayHost *GatewayProvider::getHost() const
{
	return m_host;
}

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
