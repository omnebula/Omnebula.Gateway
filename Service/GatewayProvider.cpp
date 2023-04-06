#include "pch.h"
#include "GatewayContext.h"
#include "GatewayDispatcher.h"
#include "GatewayProvider.h"


static const String ELLIPSIS = "...";
static const String QUERY_ELLIPSIS = "?...";


//////////////////////////////////////////////////////////////////////////
// class GatewayProvider
//

GatewayProvider::GatewayProvider(const Xml &providerConfig, const String &target)
{
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

GatewayRedirectProvider::GatewayRedirectProvider(const Xml &config, const String &target) :
	GatewayProvider(config, target)
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

GatewayFileProvider::GatewayFileProvider(const Xml &config, const String &target) :
	GatewayProvider(config, target)
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

	context->sendResponse(response);
}


//////////////////////////////////////////////////////////////////////////
// class GatewayServerProvider
//

GatewayServerProvider::GatewayServerProvider(const Xml &config, const String &target) :
	GatewayProvider(config, target)
{
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

	m_connectionPool = AcquireConnectionPool(m_target);
}

GatewayServerProvider::~GatewayServerProvider()
{
	ReleaseConnectionPool(m_target);
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

	// Send to origin server.
	NetStreamPtr serverStream = m_connectionPool->alloc();
	if (serverStream)
	{
		// Use a ref-pointer to ensure that the pool remains valid throughout async i/o routines.
		// Otherwise, it may be prematurely deleted during configuration changes and crash when
		// pool->free() is called.
		ConnectionPoolPtr pool = m_connectionPool;

		context->sendRequest(
			serverStream,
			[pool, context, serverStream](IoState *state) mutable
			{
				if (state->succeeded())
				{
					HttpResponsePtr clientResponse = new HttpResponse;

					context->receiveResponse(
						clientResponse,
						serverStream,
						[pool, context, serverStream, clientResponse](IoState *state) mutable
						{
							if (state->succeeded())
							{
								context->sendResponse(
									clientResponse,
									[pool, serverStream](IoState *state) mutable
									{
										if (state->succeeded())
										{
											pool->free(serverStream);
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
							}
						}
					);
				}
				else
				{
					serverStream->close();
					context->sendErrorResponse(HttpStatus::SERVICE_UNAVAIL, "host unavailable");
				}
			}
		);
	}
	else
	{
		context->sendErrorResponse(HttpStatus::SERVICE_UNAVAIL, "host unavailable");
	}
}


SyncMutex GatewayServerProvider::sm_connectionPoolMapMutex;
std::unordered_map<String, GatewayServerProvider::ConnectionPoolPtr> GatewayServerProvider::sm_connectionPoolMap;

GatewayServerProvider::ConnectionPool *GatewayServerProvider::AcquireConnectionPool(const String &connector)
{
	String scheme, address;
	connector.splitLeft(":", &scheme, &address);

	NetProtocol *protocol = NetProtocol::LookupScheme(scheme);
	if (!protocol)
	{
		throw Exception("unknown protocol '%s'", scheme);
	}

	ConnectionPool *connectionPool = nullptr;
	SyncLock lock(sm_connectionPoolMapMutex);

	auto it = sm_connectionPoolMap.find(connector);
	if (it != sm_connectionPoolMap.end())
	{
		connectionPool = it->second;
	}
	else
	{
		connectionPool = new ConnectionPool;
		connectionPool->init(protocol, address);
		sm_connectionPoolMap[connector] = connectionPool;
	}

	connectionPool->m_acquisitionCount++;

	return connectionPool;
}

void GatewayServerProvider::ReleaseConnectionPool(const String &connector)
{
	SyncLock lock(sm_connectionPoolMapMutex);

	auto it = sm_connectionPoolMap.find(connector);
	if (it != sm_connectionPoolMap.end())
	{
		if (--(it->second->m_acquisitionCount) == 0)
		{
			sm_connectionPoolMap.erase(it);
		}
	}
}