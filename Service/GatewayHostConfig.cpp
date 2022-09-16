#include "pch.h"
#include "GatewayHostConfig.h"


//////////////////////////////////////////////////////////////////////
// class GatewayHostConfig
//

bool GatewayHostConfig::load(const Xml &configRoot)
{
	try
	{
		traverse(configRoot, configRoot.getAttributes());
	}
	catch (Exception &x)
	{
		AfxLogError("Error loading host configuration - %s", x.getMessage());
		return false;
	}
	return true;
}


void GatewayHostConfig::traverse(
	const Xml &parentConfig,
	const PropertyMap &parentProps,
	std::function<void(const Xml &, const PropertyMap &)> &&func)
{
	for (auto &childConfig : parentConfig)
	{
		PropertyMap localProps;
		if (!childConfig.getAttributes().isEmpty())
		{
			localProps = parentProps;

			for (auto &it : childConfig.getAttributes())
			{
				localProps[it.first] = it.second;
			}
		}

		auto &childProps = localProps.isEmpty() ? parentProps : localProps;

		bool ok = false;
		if (func != nullptr)
		{
			func(childConfig, childProps);
		}
		else
		{
			auto tagName = childConfig.getTagName();

			if (tagName.compareNoCase("host") == 0)
			{
				loadHost(childConfig, childProps);
			}
			else
			{
				traverse(childConfig, childProps);
			}
		}
	}
}


static inline bool __NormalizeConnectors(StringSet &hostConnectors, const String &listenerProp)
{
	listenerProp.splice(
		";",
		[&hostConnectors](const String &connector) mutable
		{
			NetProtocol *protocol = NetProtocol::LookupConnector(connector);
			if (protocol)
			{
				String normalized(connector);
				if (protocol->normalizeConnector(normalized))
				{
					hostConnectors.insert(normalized);
				}
				else
				{
					throw Exception("malformed host listener: %s", connector);
				}
			}
		}
	);

	return !hostConnectors.empty();
}
void GatewayHostConfig::loadHost(
	const Xml &hostConfig,
	const PropertyMap &hostProps)
{
	String prop;

	StringVector hostNames;
	if (!hostProps.get("name", prop) || !prop.splice(";", hostNames))
	{
		throw Exception("missing host name");
	}

	StringSet hostConnectors;
	if (!hostProps.get("listener", prop))
	{
		throw Exception("missing host listener");
	}
	else if (!__NormalizeConnectors(hostConnectors, prop))
	{
		throw Exception("no listeners defined");
	}

	GatewayHostPtr host = new GatewayHost;

	traverse(
		hostConfig,
		hostProps,
		[this, &host](const Xml &childConfig, const PropertyMap &childProps) mutable
		{
			String uri;
			if (!childProps.get("uri", uri) || uri.isEmpty())
			{
				throw Exception("missing uri");
			}

			String target = childConfig.getAttribute("target");
			if (target.isEmpty())
			{
				throw Exception("missing target");
			}

			String tagName = childConfig.getTagName();
			if (tagName.compareNoCase("redirect") == 0)
			{
				host->addProvider(uri, new GatewayRedirectProvider(childConfig, target));
			}
			else if (tagName.compareNoCase("file") == 0)
			{
				host->addProvider(uri, new GatewayFileProvider(childConfig, target));
			}
			else if (tagName.compareNoCase("server") == 0)
			{
				host->addProvider(uri, new GatewayServerProvider(childConfig, target));
			}
			else
			{
				throw Exception("unknown provider type: %s", tagName);
			}
		}
	);

	if (host->getProviderCount() > 0)
	{
		for (auto &connectorString : hostConnectors)
		{
			String scheme;
			connectorString.splitLeft(":", &scheme, nullptr);

			if (!NetProtocol::LookupScheme(scheme))
			{
				throw Exception("unknown listener protocol '%s'", scheme);
			}

			for (auto &name : hostNames)
			{
				GatewayHostMapPtr hostMap;
				auto it = m_hostMaps.find(connectorString);
				if (it == m_hostMaps.end())
				{
					hostMap = new GatewayHostMap;
					m_hostMaps[connectorString] = hostMap;
				}
				else
				{
					hostMap = it->second;
				}

				if (!hostMap->lookup(name))
				{
					hostMap->insert(name, host);
				}
				else
				{
					throw Exception("host '%s' already assigned to '%s'", name, connectorString);
				}
			}
		}
	}
}
