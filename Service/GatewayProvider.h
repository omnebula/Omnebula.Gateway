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
	GatewayServerProvider(GatewayHost *host, const Xml &config, const String &target);
	virtual ~GatewayServerProvider();

protected:
	GatewayServerProvider() = default;

	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri);

	void sendToServer(GatewayContext *context, NetStreamPtr serverStream);

protected:
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
	ConnectionPool *m_connectionPool{ nullptr };

	static SyncMutex sm_connectionPoolMapMutex;
	static std::unordered_map<String, ConnectionPoolPtr> sm_connectionPoolMap;

	static ConnectionPool *AcquireConnectionPool(const String &connector);
	static void ReleaseConnectionPool(const String &connector);

	virtual bool allocateConnection(GatewayContext *context, NetStreamPtr &serverStream);
	virtual void freeConnection(NetStreamPtr serverStream, ConnectionPool *pool = nullptr);
};

using GatewayServerProviderPtr = RefPointer<GatewayServerProvider>;



//////////////////////////////////////////////////////////////////////////
// class GatewayPublisherProvider
//

class GatewayPublisherProvider : public GatewayServerProvider, public WebSocketServer
{
public:
	GatewayPublisherProvider(GatewayHost *host, const Xml &config, const String &target);
	virtual ~GatewayPublisherProvider();

protected:
	virtual void dispatchRequest(GatewayContext *context, const HttpUri &uri) override;

	// Allocate/free remote origin server connection.
	virtual bool allocateConnection(GatewayContext *context, NetStreamPtr &serverStream) override;
	virtual void freeConnection(NetStreamPtr serverStream, ConnectionPool *pool = nullptr) override;

	virtual void onError(Context *context, Context::Error &error);
	virtual void onClose(Context *context);

private:
	SyncMutex m_mutex;
	std::queue<GatewayContext*> m_pendingConnections;
	Context::Ptr m_controllerContext;
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



inline GatewayServerProvider::ConnectionPool::ConnectionPool() :
	m_acquisitionCount(0)
{
}