#pragma once
#include "GatewayHost.h"


class GatewayDispatcher;


//////////////////////////////////////////////////////////////////////////
// class GatewayContext
//

class GatewayContext : public NetContext
{
public:
	HttpRequest request;

	GatewayContext(GatewayDispatcher *dispatcher);
	virtual ~GatewayContext();

	virtual void beginContext(NetStream *stream);
	virtual bool close();

	void beginRequest();
	void receiveRequest(NetStream *stream, io_handler_t &&handler);
	void sendRequest(NetStream *stream, io_handler_t &&handler);

	void receiveResponse(HttpResponsePtr response, NetStream *stream, io_handler_t &&handler);
	void sendResponse(HttpResponsePtr response, io_handler_t &&handler = nullptr);
	void sendErrorResponse(int statusCode, const char *statusMeaning = nullptr);

	bool isRelay();
	void beginRelay(NetStream *serverStream);

	void reset();
	void discard();

protected:
	SyncMutex m_mutex;
	NetStreamPtr m_serverStream;
	GatewayDispatcher *m_dispatcher;

private:
	static const unsigned RELAY_BUFFER_SIZE = 8192;

	SyncMutex m_relayMutex;

	MemBuffer m_clientRelayBuffer;
	MemBuffer m_serverRelayBuffer;

	void beginClientRelay();
	void closeClientRelay();
	void beginServerRelay();
	void closeServerRelay();
};

using GatewayContextPtr = RefPointer<GatewayContext>;


/* Inline Implementations */

inline void GatewayContext::receiveRequest(NetStream *stream, io_handler_t &&handler)
{
	request.receive(stream, std::move(handler));
}

inline void GatewayContext::sendRequest(NetStream *stream, io_handler_t &&handler)
{
	request.send(stream, std::move(handler));
}

inline void GatewayContext::reset()
{
	request.reset();

	if (isRelay())
	{
		m_clientRelayBuffer.free();
		m_serverRelayBuffer.free();
		m_serverStream->close();
		m_serverStream = nullptr;
	}
}

inline bool GatewayContext::isRelay()
{
	return m_clientRelayBuffer.getCapacity();
}