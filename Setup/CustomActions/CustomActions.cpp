// dllmain.cpp : Defines the entry points for the DLL application.
#include "pch.h"


void IteratePrivateKeyDirectories(const String &accountName, const String &servicePath, ACCESS_MODE accessMode);


UINT __stdcall OGCA_InstallAccess(MSIHANDLE msiHandle)
{
	UINT result = AfxWinErrFromHResult(CoInitializeEx(NULL, COINIT_MULTITHREADED));

	if (result == ERROR_SUCCESS)
	{
		MsiCustomActions ca(msiHandle, "OGCA_InstallAccess");

		try
		{
			String accountName, serviceName, servicePath;
			ca.getCustomActionData(";", accountName, serviceName, servicePath);

			WinFwPolicy policy;
			if (!policy.getRule(serviceName).isValid())
			{
				WinFwRule rule;
				rule.create();
				rule.setRuleName(serviceName);
				rule.setGroupName(serviceName);
				rule.setApplicationName(servicePath);
				rule.setDirection(NET_FW_RULE_DIR_IN);
				rule.setEnabled(VARIANT_TRUE);

				policy.addRule(rule);
			}

//			IteratePrivateKeyDirectories(accountName, servicePath, GRANT_ACCESS);

			ca.logInfo("Successfully installed access");
		}
		catch (Exception &x)
		{
			ca.logError(x.getMessage());
			result = x.getCode();
		}

		CoUninitialize();
	}

	return result;
}


UINT __stdcall OGCA_UninstallAccess(MSIHANDLE msiHandle)
{
	UINT result = AfxWinErrFromHResult(CoInitializeEx(NULL, COINIT_MULTITHREADED));

	if (result == ERROR_SUCCESS)
	{
		MsiCustomActions ca(msiHandle, "OGCA_UninstallAccess");

		try
		{
			String accountName, serviceName, servicePath;
			ca.getCustomActionData(";", accountName, serviceName, servicePath);

			WinFwPolicy policy;
			policy.removeRule(serviceName);

//			IteratePrivateKeyDirectories(accountName, servicePath, REVOKE_ACCESS);

			ca.logInfo("Successfully uninstalled access");
		}
		catch (Exception &x)
		{
			ca.logError(x.getMessage());
			result = x.getCode();
		}

		CoUninitialize();
	}

	return result;
}


void ModifyAccess(WinAccount &account, const char *directoryPath, DWORD permissions, ACCESS_MODE accessMode, DWORD inheritance)
{
	String step;
	WinAccessControlList acl;

	if (!acl.openFile(directoryPath))
	{
		step.format("open directory '%s'", directoryPath);
	}
	else if (!acl.addEntry(account, permissions, accessMode, inheritance))
	{
		step.format("add entry '%s'", directoryPath);
	}
	else if (!acl.applyEntries())
	{
		step.format("apply entries '%s'", directoryPath);
	}

	// Process error result, if any.
	int result = AfxGetLastError();
	if (result != ERROR_SUCCESS)
	{
		throw Exception(result, "%s - %s", step, AfxFormatError(result));
	}
}

void IteratePrivateKeyDirectories(const String &accountName, const String &servicePath, ACCESS_MODE accessMode)
{
	const char *PRIVATE_KEY_DIRECTORIES[] =
	{
		// The Microsoft legacy CryptoAPI CSPs store private keys in the following directories.
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\RSA\\S-1-5-18",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\DSS\\S-1-5-18",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\RSA\\S-1-5-19",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\DSS\\S-1-5-19",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\RSA\\S-1-5-20",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\DSS\\S-1-5-20",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\RSA\\MachineKeys",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\DSS\\MachineKeys",
		// CNG stores private keys in the following directories.
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\SystemKeys",
		"%WINDIR%\\ServiceProfiles\\LocalService",
		"%WINDIR%\\ServiceProfiles\\NetworkService",
		"%ALLUSERSPROFILE%\\Application Data\\Microsoft\\Crypto\\Keys",
		nullptr
	};

	WinAccount account;
	if (account.lookupName(accountName))
	{
		String serviceFolder;
		AfxSplitPath(servicePath, &serviceFolder, nullptr);

		ModifyAccess(account, serviceFolder, FILE_ALL_ACCESS, accessMode, SUB_CONTAINERS_AND_OBJECTS_INHERIT);

		for (const char **current = PRIVATE_KEY_DIRECTORIES; *current; current++)
		{
			String directoryPath = AfxExpandEnvironmentStrings(*current);
			if (AfxDirectoryExists(directoryPath))
			{
				ModifyAccess(account, directoryPath, FILE_ALL_ACCESS, accessMode, SUB_CONTAINERS_AND_OBJECTS_INHERIT);
			}
		}
	}
	else
	{
		throw Exception(AfxGetLastError(), "lookup account '%s'", accountName);
	}
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

