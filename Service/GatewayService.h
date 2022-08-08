#pragma once
#include "GatewayHostConfig.h"
#include "GatewayDispatcher.h"


//////////////////////////////////////////////////////////////////////
// class OmnebulaGatewayServiceApp
//

class OmnebulaGatewayServiceApp : public ServiceApp
{
public:
	OmnebulaGatewayServiceApp();

protected:
	virtual bool initApp();
	virtual void exitApp();

	bool initConfigs();
	bool loadServiceConfig(Xml &serviceConfig);
	bool loadHostConfig(Xml &hostsConfig);

private:
	GatewayHostConfigPtr m_hostConfig;

	SyncMutex m_dispatcherMutex;
	GatewayDispatcherMap m_dispatcherMap;

	ConfigMonitor m_configMonitor;
};