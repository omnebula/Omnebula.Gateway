#include "pch.h"
#include "GatewayDispatcher.h"
#include "GatewayService.h"
#include "GatewayContext.h"


//////////////////////////////////////////////////////////////////////////
// class GatewayContext
//

GatewayContext::GatewayContext(GatewayDispatcher *dispatcher) :
	m_dispatcher(dispatcher)
{
	assert(m_dispatcher);
}

GatewayContext::~GatewayContext()
{
}


void GatewayContext::beginContext(NetStream *stream)
{
	__super::beginContext(stream);

	beginRequest();
}

void GatewayContext::beginRequest()
{
	reset();

	receiveRequest(
		getStream(),
		[this](IoState *state) mutable
		{
			if (state->succeeded())
			{
				AfxPushIoProcess(
					[this]() mutable
					{
 						String hostName = request.getHost();
// 						hostName.splitRight(":", &hostName, nullptr);

						GatewayHostPtr host = m_dispatcher->lookupHost(hostName);
						if (host)
						{
							HttpUri uri = Http::DecodeUri(request.getUri());
							GatewayProviderPtr provider = host->lookupProvider(uri);
							if (provider)
							{
								provider->beginDispatch(this, uri);
							}
							else
							{
								sendErrorResponse(HttpStatus::NOT_FOUND);
							}
						}
						else
						{
							sendErrorResponse(HttpStatus::BAD_REQUEST);
						}
					}
				);
			}
			else
			{
				discard();
			}
		}
	);
}


void GatewayContext::receiveResponse(HttpResponsePtr response, NetStream *stream, io_handler_t &&handler)
{
	m_mutex.lock();
	m_serverStream = stream;
	m_mutex.unlock();

	response->receive(
		stream,
		[this, response, handler](IoState *state) mutable
		{
			handler(state);

			m_mutex.lock();
			m_serverStream = nullptr;
			m_mutex.unlock();

			if (state->failed())
			{
				discard();
			}
		}
	);
}


void GatewayContext::sendResponse(HttpResponsePtr response, io_handler_t &&handler)
{
	response->send(
		getStream(),
		[this, response, handler](IoState *state) mutable
		{
			if (handler)
			{
				handler(state);
			}

			if (!isRelay())
			{
				if (state->succeeded() && response->isKeepAlive())
				{
					beginRequest();
				}
				else
				{
					discard();
				}
			}
		}
	);
}


void GatewayContext::sendErrorResponse(int statusCode, const char *statusMeaning)
{
	HttpResponsePtr response = new HttpServerResponse;
	response->setStatus(statusCode, statusMeaning);
	sendResponse(response);
}


void GatewayContext::beginRelay(NetStream *serverStream)
{
	// Hold on to the server stream.
	m_serverStream = serverStream;

	// Initialize relay buffers.
	m_clientRelayBuffer.alloc(RELAY_BUFFER_SIZE);
	m_serverRelayBuffer.alloc(RELAY_BUFFER_SIZE);

	// Start relaying from both ends.
	beginServerRelay();
	beginClientRelay();
}

void GatewayContext::beginClientRelay()
{
	m_stream->read(
		m_clientRelayBuffer, m_clientRelayBuffer.getCapacity(),
		[this](IoState *state) mutable
		{
			size_t count = state->getTransferCount();
			if (count)
			{
				m_serverStream->write(
					m_clientRelayBuffer, count,
					[this](IoState *state) mutable
					{
						if (state->succeeded())
						{
							beginClientRelay();
						}
						else
						{
							closeClientRelay();
						}
					}
				);
			}
			else
			{
				closeClientRelay();
			}
		}
	);
}
void GatewayContext::closeClientRelay()
{
	SyncLock lock(m_relayMutex);

	m_clientRelayBuffer.free();
	m_stream->close();
	m_stream = nullptr;

	if (m_serverStream)
	{
		m_serverStream->close();
	}
	else
	{
		discard();
	}
}

void GatewayContext::beginServerRelay()
{
	m_serverStream->read(
		m_serverRelayBuffer, m_serverRelayBuffer.getCapacity(),
		[this](IoState *state) mutable
		{
			size_t count = state->getTransferCount();
			if (count)
			{
				m_stream->write(
					m_serverRelayBuffer, count,
					[this](IoState *state) mutable
					{
						if (state->succeeded())
						{
							beginServerRelay();
						}
						else
						{
							closeServerRelay();
						}
					}
				);
			}
			else
			{
				closeServerRelay();
			}
		}
	);
}
void GatewayContext::closeServerRelay()
{
	SyncLock lock(m_relayMutex);

	m_serverRelayBuffer.free();
	m_serverStream->close();
	m_serverStream = nullptr;

	if (m_stream)
	{
		m_stream->close();
	}
	else
	{
		discard();
	}
}


bool GatewayContext::close()
{
	m_mutex.lock();
	NetStreamPtr stream = std::move(m_serverStream);
	m_mutex.unlock();

	if (stream)
	{
		stream->close();
	}

	return NetContext::close();
}


void GatewayContext::discard()
{
	m_dispatcher->endContext(this);
}


