#include "stdafx.h"
#include "gc_client.h"
#include "gc_message.h"
#include "networking_client.h"

#include <cstdio>

namespace Platform
{

void Print(const char *, ...)
{
}

void *ModuleFactory(std::string_view)
{
    // Tests run without the game, so the gametypes interface must resolve to
    // nothing. This keeps the music kit gate on its conservative path.
    return nullptr;
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

template<typename T>
static void SendGCProtobuf(ClientGC &gc, uint32_t type, const T &message)
{
    GCMessageWrite messageWrite{ type, message };
    gc.PostToGC(GCEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}

static bool WaitForAssignment(ClientGC &gc,
    CMsgGCCStrike15_v2_PlayerOverwatchCaseAssignment &assignment)
{
    std::vector<EventData> events;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (const EventData &event : events)
        {
            if (event.type != static_cast<int>(HostEvent::Message)
                || (event.id & ~ProtobufMask) != k_EMsgGCCStrike15_v2_PlayerOverwatchCaseAssignment)
            {
                continue;
            }

            GCMessageRead messageRead{
                static_cast<uint32_t>(event.id),
                event.buffer.data(),
                static_cast<uint32_t>(event.buffer.size()) };
            if (messageRead.IsValid() && messageRead.ReadProtobuf(assignment))
            {
                return true;
            }
        }

        events.clear();
        std::this_thread::yield();
    }

    return false;
}

static bool OverwatchAssignmentIsSentOnHelloAndPoll()
{
    constexpr uint64_t SteamId = 76561197960265729ull;

    if (!GetConfig().OverwatchEnabled())
    {
        return false;
    }

    ClientGC gc{ SteamId };

    CMsgClientHello hello;
    SendGCProtobuf(gc, k_EMsgGCClientHello, hello);

    CMsgGCCStrike15_v2_PlayerOverwatchCaseAssignment assignment;
    bool valid = WaitForAssignment(gc, assignment)
        && assignment.caseid() != 0
        && assignment.suspectid() != 0
        && assignment.fractionid() != 0
        && assignment.numrounds() != 0
        && assignment.fractionrounds() != 0
        && assignment.reason() == 1;

    CMsgGCCStrike15_v2_PlayerOverwatchCaseUpdate poll;
    poll.set_reason(0);
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_PlayerOverwatchCaseUpdate, poll);

    assignment.Clear();
    valid &= WaitForAssignment(gc, assignment)
        && assignment.caseid() != 0
        && assignment.reason() == 1;

    return valid;
}

int main()
{
    const bool passed = OverwatchAssignmentIsSentOnHelloAndPoll();
    std::printf("OverwatchAssignmentIsSentOnHelloAndPoll: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
