#include "stdafx.h"
#include "config.h"
#include "inventory.h"
#include "keyvalue.h"
#include "networking_client.h"
#include "test_filesystem.h"

#include <cstdio>

namespace Platform
{

void Print(const char *, ...)
{
}

bool UpdateGraffitiKey(std::string_view, const void *, const void *, size_t)
{
    return true;
}

}

S_API void S_CALLTYPE SteamAPI_RegisterCallback(CCallbackBase *, int)
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallback(CCallbackBase *)
{
}

static bool NonPrimeStatusFreezesReportedElevatedState()
{
    constexpr uint64_t SteamId = 76561197960265729ull;

    // This process is launched with a dedicated working directory whose
    // csgo_gc/config.txt disables prime_status, since GetConfig() latches
    // on first use and cannot switch configurations within one process.
    bool valid = GetConfig().PrimeStatus() == false;
    {
        Inventory inventory{ SteamId };
        CMsgSOCacheSubscribed subscription;
        inventory.BuildCacheSubscription(subscription, false);

        bool foundAccount = false;
        bool foundPersona = false;
        for (const CMsgSOCacheSubscribed_SubscribedType &type : subscription.objects())
        {
            if (type.type_id() == SOTypeGameAccountClient && type.object_data_size() == 1)
            {
                CSOEconGameAccountClient account;
                foundAccount = account.ParseFromString(type.object_data(0));
                valid &= foundAccount && account.elevated_state() == ElevatedStateNo;
            }
            else if (type.type_id() == SOTypePersonaDataPublic && type.object_data_size() == 1)
            {
                CSOPersonaDataPublic personaData;
                foundPersona = personaData.ParseFromString(type.object_data(0));
                valid &= foundPersona && !personaData.elevated_state();
            }
        }
        valid &= foundAccount && foundPersona;
    }

    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    return valid;
}

int main()
{
    const bool passed = NonPrimeStatusFreezesReportedElevatedState();
    std::printf("NonPrimeStatusFreezesReportedElevatedState: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
