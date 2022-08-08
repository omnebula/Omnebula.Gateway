#pragma once
#include "GatewayHost.h"


//////////////////////////////////////////////////////////////////////
// class GatewayHostConfig
//

class GatewayHostConfig : public RefCounter
{
public:
	bool load(const Xml &configRoot);

	GatewayHostMapPtr popHostMap(const char *connectorString);
	void forEachHostMap(std::function<void(const String&, GatewayHostMap*)> &&func);

private:
	void traverse(
		const Xml &parentConfig,
		const PropertyMap &parentProps,
		std::function<void(const Xml &, const PropertyMap &)> &&func = nullptr);

	void loadHost(
		const Xml &hostConfig,
		const PropertyMap &hostProps);

private:
	StringMap m_filePaths;
	std::map<String, GatewayHostMapPtr> m_hostMaps;
};

using GatewayHostConfigPtr = RefPointer<GatewayHostConfig>;



inline GatewayHostMapPtr GatewayHostConfig::popHostMap(const char *connectorString)
{
	GatewayHostMapPtr hostMap;
	auto it = m_hostMaps.find(connectorString);
	if (it != m_hostMaps.end())
	{
		hostMap = it->second;
		m_hostMaps.erase(it);
	}
	return hostMap;
}

inline void GatewayHostConfig::forEachHostMap(std::function<void(const String&, GatewayHostMap*)> &&func)
{
	for (auto &it : m_hostMaps)
	{
		func(it.first, it.second);
	}
}
