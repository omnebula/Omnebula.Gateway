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

	HttpConnectionSettings &getConnectionSettings();

protected:
	virtual bool initApp();
	virtual void exitApp();

	virtual bool initLogging();
	bool initConfigs();
	bool loadServiceConfig(Xml &serviceConfig);
	bool initServiceCertificates(Xml &serviceConfig);
	bool initServiceTimeouts(Xml &serviceConfig);
	bool loadHostConfig(Xml &hostsConfig);

private:
	GatewayHostConfigPtr m_hostConfig;

	SyncMutex m_dispatcherMutex;
	GatewayDispatcherMap m_dispatcherMap;

	ConfigMonitor m_configMonitor;

	HttpConnectionSettings m_connectionSettings;
};


extern OmnebulaGatewayServiceApp theApp;


inline HttpConnectionSettings &OmnebulaGatewayServiceApp::getConnectionSettings()
{
	return m_connectionSettings;
}
