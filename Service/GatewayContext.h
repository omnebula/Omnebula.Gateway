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

	void reset();
	void discard();

protected:
	SyncMutex m_mutex;
	NetStreamPtr m_serverStream;
	GatewayDispatcher *m_dispatcher;
};

using GatewayContextPtr = RefPointer<GatewayContext>;


/* Inline Implementations */

inline GatewayContext::GatewayContext(GatewayDispatcher *dispatcher) :
	m_dispatcher(dispatcher)
{
}

inline GatewayContext::~GatewayContext()
{
}

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
}