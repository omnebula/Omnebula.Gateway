#include "pch.h"
#include "GatewayContext.h"
#include "GatewayDispatcher.h"
#include "GatewayProvider.h"


static const String ELLIPSIS = "...";
static const String QUERY_ELLIPSIS = "?...";


//////////////////////////////////////////////////////////////////////////
// class GatewayProvider
//

GatewayProvider::GatewayProvider(GatewayHost *host, const Xml &providerConfig, const String &target)
{
	m_host = host;
	m_target = target;

	providerConfig.getAttribute("uri", m_uri);
	if (m_uri.isEmpty())
	{
		m_uri = "/";
	}
	else if (m_uri[0] != '/')
	{
		m_uri = "/" + m_uri.trimRight('/');
	}

	// Basic-Auth.
	Xml authConfig;
	if (providerConfig.findChild("auth", authConfig))
	{
		String type = authConfig.getAttribute("type");
		if (type.isEmpty() || (type = "basic"))
		{
			m_basicAuthRealm = authConfig.getAttribute("realm");

			for (auto child : authConfig)
			{
				if (child.getTagName() == "user")
				{
					String name = child.getAttribute("name");
					String password = child.getAttribute("password");

					m_basicAuthUsers[name] = password;
				}
			}
		}
	}
}


void GatewayProvider::beginDispatch(GatewayContext *context, const HttpUri &uri)
{
	// First, try authenticating.
	if (!m_basicAuthUsers.isEmpty())
	{
		String user, requestPassword, configuredPassword;
		if (context->request.getBasicAuth(user, requestPassword))
		{
			bool authorized = m_basicAuthUsers.get(user, configuredPassword);
			if (authorized)
			{
				authorized = configuredPassword.isEmpty() ? AfxAuthenticateUser(user, requestPassword) : (requestPassword == configuredPassword);
			}

			if (!authorized)
			{
				context->sendErrorResponse(HttpStatus::DENIED);
				return;
			}
		}
	}

	// Handle the request.
	dispatchRequest(context, uri);
}



//////////////////////////////////////////////////////////////////////////
// class GatewayRedirectProvider
//

GatewayRedirectProvider::GatewayRedirectProvider(GatewayHost *host, const Xml &config, const String &target) :
	GatewayProvider(host, config, target)
{
	m_newQuery = ELLIPSIS;

	Http::SplitUrl(m_target, &m_newScheme, &m_newHost, &m_newPath, &m_newQuery);

	if (m_newScheme == ELLIPSIS)
	{
		m_newScheme.clear();
	}

	if (m_newHost == ELLIPSIS)
	{
		m_newHost.clear();
	}
}

void GatewayRedirectProvider::dispatchRequest(GatewayContext *context, const HttpUri &uri)
{
	static const String SECURE_SCHEME = "https";
	static const String INSECURE_SCHEME = "http";

	String scheme = m_newScheme.isEmpty() ? (context->getStream()->isSecure() ? SECURE_SCHEME : INSECURE_SCHEME) : m_newScheme;
	String host = m_newHost.isEmpty() ? context->request.getHost() : m_newHost;

	String requestPath = uri.getPath();
	String requestQuery = uri.getQueryString();
	if (requestPath.isEmpty())
	{
		throw HttpException(HttpStatus::BAD_REQUEST);
	}

	String path;
	if (!m_newPath.isEmpty())
	{
		path = m_newPath;
		path.replace(ELLIPSIS, requestPath);
		path.trimLeft('/');
	}

	String query;
	if (!m_newQuery.isEmpty())
	{
		query = m_newQuery;
		query.replace(ELLIPSIS, requestQuery);
	}

	String location;
	if (path.isEmpty())
	{
		if (query.isEmpty())
		{
			location.format("%s://%s", scheme, host);
		}
		else
		{
			location.format("%s://%s?%s", scheme, host, query);
		}
	}
	else
	{
		if (query.isEmpty())
		{
			location.format("%s://%s/%s", scheme, host, path);
		}
		else
		{
			location.format("%s://%s/%s?%s", scheme, host, path, query);
		}
	}

	HttpServerResponse *response = new HttpServerResponse;

	response->setStatus(HttpStatus::REDIRECT_KEEP_VERB);
	response->setHeader(HttpHeader::LOCATION, location);

	syncConnectionType(context->request, *response);

	context->sendResponse(response);
}


//////////////////////////////////////////////////////////////////////////
// class GatewayFileProvider
//

GatewayFileProvider::GatewayFileProvider(GatewayHost *host, const Xml &config, const String &target) :
	GatewayProvider(host, config, target)
{
	m_target.trimRight("\\");
	m_target.trimRight("/");

	Xml options;
	if (config.findChild("options", options))
	{
		m_defaultFile = options.getAttribute("def-file");
		m_defaultExtension = options.getAttribute("def-ext");

		if (!m_defaultExtension.isEmpty() && (m_defaultExtension[0] != '.'))
		{
			m_defaultExtension = "." + m_defaultExtension;
		}

		Xml responseHeaders;
		if (options.findChild("response-headers", responseHeaders))
		{
			for (auto current : responseHeaders)
			{
				String name = current.getTagName();
				String value = current.getData();
				m_responseHeaders[name] = value;
			}
		}
	}
}

void GatewayFileProvider::dispatchRequest(GatewayContext *context, const HttpUri &uri)
{
	String pathInfo = uri.getPathInfo();
	pathInfo.trimLeft('/');

	HttpServerResponse *response = new HttpServerResponse;

	HttpFileHandler::RetrieveFile(
		context->request,
		*response,
		m_target,
		pathInfo,
		m_defaultFile.isEmpty() ? nullptr : m_defaultFile,
		m_defaultExtension.isEmpty() ? nullptr : m_defaultExtension,
		false);

	syncConnectionType(context->request, *response);

	if (response->succeeded())
	{
		if (!m_responseHeaders.isEmpty())
		{
			for (auto current : m_responseHeaders)
			{
				response->setHeader(current.first, current.second);
			}
		}
	}

	context->sendResponse(response);
}


//////////////////////////////////////////////////////////////////////////
// class GatewayServerProvider
//

SyncMutex GatewayServerProvider::sm_connectionPoolMapMutex;
GatewayServerProvider::ConnectionPoolMap *GatewayServerProvider::sm_connectionPoolMap = nullptr;


GatewayServerProvider::GatewayServerProvider()
{
	initConnectionPoolMap();
}

GatewayServerProvider::GatewayServerProvider(GatewayHost *host, const Xml &config, const String &target, bool initConnectionPool) :
	GatewayProvider(host, config, target)
{
	initConnectionPoolMap();

	Xml optionConfig;
	if (config.findChild("options", optionConfig))
	{
		m_newHost = optionConfig.getAttribute("new-host");
		
		String newUri = optionConfig.getAttribute("new-uri");
		if (!newUri.isEmpty())
		{
			if (!newUri.splitLeft("?", &m_newPath, &m_newQuery))
			{
				m_newPath = newUri;
			}

			if (m_newPath[0] != '/')
			{
				throw Exception(ERROR_BAD_ARGUMENTS, "invalid uri: %s", newUri);
			}
		}
	}

	if (m_target)
	{
		m_connectionPool = AcquireConnectionPool(m_target, initConnectionPool);
	}
}

void GatewayServerProvider::initConnectionPoolMap()
{
	SyncLock lock(sm_connectionPoolMapMutex);
	if (!sm_connectionPoolMap)
	{
		sm_connectionPoolMap = new ConnectionPoolMap;
	}

	sm_connectionPoolMap->__incRef();
}


GatewayServerProvider::~GatewayServerProvider()
{
	ReleaseConnectionPool(m_target);

	SyncLock lock(sm_connectionPoolMapMutex);
	if (sm_connectionPoolMap && !sm_connectionPoolMap->__decRef())
	{
		sm_connectionPoolMap = nullptr;
	}
}

void GatewayServerProvider::dispatchRequest(GatewayContext *context, const HttpUri &uri)
{
	// Add the Forwarded header.
	NetStreamPtr clientStream = context->getStream();
	String fwdFor = clientStream->getRemoteAddress();
	String fwdBy = clientStream->getLocalAddress();
	String fwdHost = context->request.getHost();
	String fwdProto = clientStream->isSecure() ? Http::SECURE_SCHEME : Http::SCHEME;

	fwdFor.splitLeft(":", &fwdFor, nullptr);	// truncate port
	fwdBy.splitLeft(":", &fwdBy, nullptr);		// truncate port

	context->request.addHeader(HttpHeader::FORWARDED, String("for=%s;by=%s;host=%s;proto=%s", fwdFor, fwdBy, fwdHost, fwdProto));

	// Apply options.
	if (!m_newHost.isEmpty())
	{
		context->request.setHost(m_newHost);
	}

	if (!m_newPath.isEmpty() || !m_newQuery.isEmpty())
	{
		HttpUri newUri;
		if (m_newPath.isEmpty())
		{
			newUri.setPath(uri.getPath());
		}
		else
		{
			String path = m_newPath;
			path.replace(ELLIPSIS, uri.getPathInfo());	// Don't use full path; skip server-info.
			newUri.setPath(path);
		}

		if (!m_newQuery.isEmpty())
		{
			String query = m_newQuery;
			query.replace(ELLIPSIS, uri.getQueryString());
			newUri.setQueryString(query);
		}

		context->request.setUri(newUri);
	}

	// Allocate stream to origin server.
	NetStreamPtr serverStream;
	if (!allocateConnection(context, serverStream))
	{
		return;
	}

	if (!serverStream)
	{
		context->sendErrorResponse(HttpStatus::SERVICE_UNAVAIL, "host unavailable");
		return;
	}

	sendToServer(context, serverStream);
}

void GatewayServerProvider::sendToServer(GatewayContext *context, NetStreamPtr serverStream)
{
	// Use a ref-pointer to ensure that the pool remains valid throughout async i/o routines.
	// Otherwise, it may be prematurely deleted during configuration changes and crash when
	// pool->free() is called.
	ConnectionPool::Ptr pool = m_connectionPool;

	context->sendRequest(
		serverStream,
		[this, pool, context, serverStream](IoState *state) mutable
		{
			if (state->succeeded())
			{
				// Receive origin server's response.
				HttpResponsePtr serverResponse = new HttpResponse;

				context->receiveResponse(
					serverResponse,
					serverStream,
					[this, pool, context, serverStream, serverResponse](IoState *state) mutable
					{
						if (state->succeeded())
						{
							// Send origin server's response to the client.
							context->sendResponse(
								serverResponse,
								[this, pool, context, serverStream, serverResponse](IoState *state) mutable
								{
									if (state->succeeded())
									{
										// Detect websocket.
										if ((serverResponse->getStatusCode() == HttpStatus::SWITCH_PROTOCOLS)
											&& serverResponse->hasHeader(HttpHeader::UPGRADE, Http::WEBSOCKET)
											&& serverResponse->hasHeader(HttpHeader::CONNECTION, Http::UPGRADE_CONNECTION))
										{
											context->beginRelay(serverStream);
										}
										else
										{
											freeConnection(serverStream, pool);
										}
									}
									else
									{
										serverStream->close();
									}
								}
							);
						}
						else
						{
							serverStream->close();
							context->sendErrorResponse(HttpStatus::SERVICE_UNAVAIL, "host unavailable");
							state->setErrorCode(ERROR_SUCCESS);
						}
					}
				);
			}
			else
			{
				serverStream->close();
				context->sendErrorResponse(HttpStatus::SERVICE_UNAVAIL, "host unavailable");
				state->setErrorCode(ERROR_SUCCESS);
			}
		}
	);
}

bool GatewayServerProvider::allocateConnection(GatewayContext *context, NetStreamPtr &serverStream)
{
	serverStream = m_connectionPool->alloc();
	return true;
}

void GatewayServerProvider::freeConnection(NetStreamPtr serverStream, ConnectionPool *pool)
{
	if (!pool)
	{
		pool = m_connectionPool;
	}
	pool->free(serverStream);
}



GatewayServerProvider::ConnectionPool *GatewayServerProvider::AcquireConnectionPool(const String &connector, bool init)
{
	ConnectionPool *connectionPool = nullptr;
	SyncLock lock(sm_connectionPoolMapMutex);

	if (sm_connectionPoolMap)
	{
		auto it = sm_connectionPoolMap->find(connector);
		if (it != sm_connectionPoolMap->end())
		{
			connectionPool = it->second;
		}
		else
		{
			connectionPool = new ConnectionPool;
			sm_connectionPoolMap->emplace(connector, connectionPool);

			if (init)
			{
				String scheme, address;
				connector.splitLeft(":", &scheme, &address);

				NetProtocol *protocol = NetProtocol::LookupScheme(scheme);
				if (!protocol)
				{
					throw Exception("unknown protocol '%s'", scheme);
				}

				connectionPool->init(protocol, address);
			}
		}

		connectionPool->m_acquisitionCount++;
	}

	return connectionPool;
}

bool GatewayServerProvider::ReleaseConnectionPool(const String &connector)
{
	bool released = false;
	SyncLock lock(sm_connectionPoolMapMutex);

	if (sm_connectionPoolMap)
	{
		auto it = sm_connectionPoolMap->find(connector);
		if (it != sm_connectionPoolMap->end())
		{
			released = (--(it->second->m_acquisitionCount) == 0);
			if (released)
			{
				sm_connectionPoolMap->erase(it);
			}
		}
	}

	return released;
}


//////////////////////////////////////////////////////////////////////////
// class GatewayPublisherProvider
//

GatewayPublisherProvider::GatewayPublisherProvider(GatewayHost *host, const Xml &config, const String &target) :
	GatewayServerProvider(host, config, target, false)
{
	host->addProvider("/@subscriber" + getTarget(), new GatewayPublisherProvider::SubscriberAcceptor(this));
}

GatewayPublisherProvider::~GatewayPublisherProvider()
{
	WebSocketServer::exit();
}


void GatewayPublisherProvider::dispatchRequest(GatewayContext *context, const HttpUri &uri)
{
	__super::dispatchRequest(context, uri);
}

bool GatewayPublisherProvider::allocateConnection(GatewayContext *context, NetStreamPtr &serverStream)
{
	serverStream = m_connectionPool->alloc(false);
	if (serverStream)
	{
		return true;
	}

	{
		SyncLock lock(m_mutex);

		if (!m_controllerContext)
		{
			return true;
		}

		m_pendingConnections.push(context);
	}

	static const String ATTACH_COMMAND;
	m_controllerContext->sendText(ATTACH_COMMAND);

	return false;
}

void GatewayPublisherProvider::freeConnection(NetStreamPtr serverStream, ConnectionPool *pool)
{
	GatewayContext *pendingContext{ nullptr };

	{
		SyncLock lock(m_mutex);

		if (!m_pendingConnections.empty())
		{
			pendingContext = m_pendingConnections.front();
			m_pendingConnections.pop();
		}
	}

	if (pendingContext)
	{
 		sendToServer(pendingContext, serverStream);
	}
	else
	{
		__super::freeConnection(serverStream, pool);
	}
}


void GatewayPublisherProvider::onError(Context *context, Context::Error &error)
{
	__super::onError(context, error);
//	context->close();
//	onClose(context);
}

void GatewayPublisherProvider::onClose(Context *context)
{
	__super::onClose(context);
	if (m_controllerContext == context)
	{
		m_controllerContext = nullptr;
	}
}


void GatewayPublisherProvider::attachSubscriber(GatewayContext *subscriberContext)
{
	HttpRequest &request = subscriberContext->request;

	if (request.hasHeader(HttpHeader::UPGRADE, Http::WEBSOCKET))
	{
		HttpResponsePtr response = new HttpResponse;

		if (m_controllerContext)
		{
			response->setStatus(HttpStatus::CONFLICT, "already connected");
		}
		else
		{
			m_controllerContext = beginConnection(*subscriberContext, subscriberContext->request, *response);
			if (m_controllerContext)
			{
				subscriberContext->discard();
			}
			else
			{
				response->setStatus(HttpStatus::SERVER_ERROR);
			}
		}

	}
	else if (request.getMethod() == "X-SUBSCRIBER-ATTACH")
	{
		if (m_controllerContext)
		{
			NetStreamPtr serverStream = subscriberContext->detachStream();
			freeConnection(serverStream);
		}

		subscriberContext->discard();
	}
	else
	{
		subscriberContext->sendErrorResponse(HttpStatus::NOT_FOUND);
	}
}


void GatewayPublisherProvider::SubscriberAcceptor::dispatchRequest(GatewayContext *context, const HttpUri &uri)
{
	m_publisher->attachSubscriber(context);
}



//////////////////////////////////////////////////////////////////////////
// class GatewaySubscriberProvider
//

GatewaySubscriberProvider::GatewaySubscriberProvider(GatewayHost *host, const Xml &config, const String &publisher) :
	GatewayServerProvider(host, config, publisher, false)
{
	String protocol, address;
	publisher.splitLeft(":", &protocol, &address);
	if (protocol.compareNoCase("tls") == 0)
	{
		m_socketUrl.format("wss://%s/@subscriber%s", address, m_uri);
		m_attachUrl.format("https://%s/@subscriber%s", address, m_uri);
	}
	else
	{
		m_socketUrl.format("ws://%s/@subscriber%s", address, m_uri);
		m_attachUrl.format("http://%s/@subscriber%s", address, m_uri);
	}

	initDispatcher();
	initPublisher();
}

GatewaySubscriberProvider::~GatewaySubscriberProvider()
{
	m_isActive = false;

	m_publisherSocket.close();

	m_connectQueue.waitForIdle();

	if (m_dispatcher)
	{
		m_dispatcher->stop();
		m_dispatcher->__decRef();
	}
}


void GatewaySubscriberProvider::initDispatcher()
{
	class Dispatcher : public GatewayDispatcher
	{
	public:
		GatewayHost *m_host;
		Dispatcher(GatewayHost *host) : m_host(host)
		{
		}
		virtual GatewayHostPtr lookupHost(const char *hostName) override {
			return m_host;
		}
	};

	m_dispatcher = new Dispatcher(getHost());
	m_dispatcher->__incRef();
}

void GatewaySubscriberProvider::initPublisher()
{
	if (!m_publisherSocket.onText)
	{
		m_publisherSocket.onText = [this](String text) mutable
			{
				sendAttachRequest();
			};
		m_publisherSocket.onClose = [this]() mutable
			{
				if (m_isActive)
				{
					connectToPublisher();
				}
			};
	}

	m_isActive = true;

	connectToPublisher();

	AfxLogInfo("Started subscriber '%s%s'", getTarget(), getUri());
}

void GatewaySubscriberProvider::connectToPublisher()
{
	m_connectQueue.push([this]() mutable
		{
			while (m_isActive)
			{
				if (m_publisherSocket.connect(m_socketUrl))
				{
					break;
				}
			}
		}
	);
}

void GatewaySubscriberProvider::sendAttachRequest()
{
	HttpClient http;
	if (!http.sendRequest("X-SUBSCRIBER-ATTACH", m_attachUrl))
	{
		return;
	}

	NetStreamPtr stream = http.detachStream();

	m_dispatcher->beginContext(stream);
}


void GatewaySubscriberProvider::dispatchRequest(GatewayContext *context, const HttpUri &uri)
{
}
