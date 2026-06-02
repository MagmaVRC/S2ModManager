#include "Firewall.h"

#include <windows.h>
#include <netfw.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace platform {

bool ensureInboundAllowed(const std::wstring& ruleName) {
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool didInit = SUCCEEDED(hrInit);

    bool ok = false;
    INetFwPolicy2* policy = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&policy)))) {
        wchar_t exe[MAX_PATH];
        INetFwRules* rules = nullptr;
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH) && SUCCEEDED(policy->get_Rules(&rules)) && rules) {
            BSTR bName = SysAllocString(ruleName.c_str());
            rules->Remove(bName);   // drop any prior rule (e.g. a stale "block") with this name

            INetFwRule* rule = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                                           __uuidof(INetFwRule), reinterpret_cast<void**>(&rule)))) {
                BSTR bApp = SysAllocString(exe);
                rule->put_Name(bName);
                rule->put_ApplicationName(bApp);
                rule->put_Direction(NET_FW_RULE_DIR_IN);
                rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
                rule->put_Action(NET_FW_ACTION_ALLOW);
                rule->put_Enabled(VARIANT_TRUE);
                rule->put_Profiles(NET_FW_PROFILE2_ALL);
                ok = SUCCEEDED(rules->Add(rule));
                SysFreeString(bApp);
                rule->Release();
            }
            SysFreeString(bName);
            rules->Release();
        }
        policy->Release();
    }

    if (didInit)
        CoUninitialize();
    return ok;
}

}
