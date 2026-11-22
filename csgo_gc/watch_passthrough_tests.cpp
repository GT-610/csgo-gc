// Tests for the watch page passthrough (issue #152).
//
// These cover the parts that decide what crosses the Steam GC boundary: which
// request types are forwarded instead of answered locally, which upstream replies
// are folded back into the game's queue, and the hello version rewrite that keeps
// the message header (and therefore the job id) intact.
#include "stdafx.h"
#include "gc_const.h"
#include "watch_passthrough.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Platform
{

void Print(const char *, ...)
{
}

} // namespace Platform

namespace
{

int g_failures = 0;

void Check(bool condition, const char *what)
{
    if (!condition)
    {
        printf("FAIL: %s\n", what);
        g_failures++;
    }
}

// Captures what reaches the "real GC" so the rewrite can be inspected.
struct FakeUpstream
{
    std::vector<uint32_t> sentTypes;
    std::vector<std::vector<uint8_t>> sentBodies;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> queued;
    int sendResult = WatchPassthrough::ResultOK;

    void Queue(uint32_t type, std::vector<uint8_t> body)
    {
        queued.emplace_back(type, std::move(body));
    }
};

FakeUpstream g_upstream;

void InstallFakeUpstream()
{
    WatchPassthrough::AttachSend(&g_upstream, [](uint32_t type, const void *data, uint32_t size) {
        g_upstream.sentTypes.push_back(type);
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        g_upstream.sentBodies.emplace_back(bytes, bytes + size);
        return g_upstream.sendResult;
    });

    WatchPassthrough::AttachAvailable(&g_upstream, [](uint32_t *size) {
        if (g_upstream.queued.empty())
        {
            return false;
        }
        *size = static_cast<uint32_t>(g_upstream.queued.front().second.size());
        return true;
    });

    WatchPassthrough::AttachRetrieve(&g_upstream,
        [](uint32_t *type, void *dest, uint32_t destSize, uint32_t *size) {
            if (g_upstream.queued.empty())
            {
                return WatchPassthrough::ResultNoMessage;
            }

            auto &[queuedType, body] = g_upstream.queued.front();
            if (destSize < body.size())
            {
                // The real interface reports the size it needed and keeps the
                // message queued, which is what Pump has to cope with.
                *size = static_cast<uint32_t>(body.size());
                return WatchPassthrough::ResultBufferTooSmall;
            }

            memcpy(dest, body.data(), body.size());
            *type = queuedType;
            *size = static_cast<uint32_t>(body.size());
            g_upstream.queued.erase(g_upstream.queued.begin());
            return WatchPassthrough::ResultOK;
        });
}

// Builds a framed protobuf message the way the game frames one.
std::vector<uint8_t> Frame(uint32_t type, const google::protobuf::MessageLite &body,
    bool withHeader = true)
{
    std::vector<uint8_t> frame;
    const uint32_t masked = type | ProtobufMask;
    const uint8_t *typeBytes = reinterpret_cast<const uint8_t *>(&masked);
    frame.insert(frame.end(), typeBytes, typeBytes + sizeof(masked));

    CMsgProtoBufHeader header;
    if (withHeader)
    {
        header.set_job_id_source(0x1234);
    }

    const uint32_t headerSize = static_cast<uint32_t>(header.ByteSizeLong());
    const uint8_t *sizeBytes = reinterpret_cast<const uint8_t *>(&headerSize);
    frame.insert(frame.end(), sizeBytes, sizeBytes + sizeof(headerSize));

    if (headerSize)
    {
        std::vector<uint8_t> headerBytes(headerSize);
        header.SerializeToArray(headerBytes.data(), static_cast<int>(headerSize));
        frame.insert(frame.end(), headerBytes.begin(), headerBytes.end());
    }

    std::vector<uint8_t> bodyBytes(body.ByteSizeLong());
    body.SerializeToArray(bodyBytes.data(), static_cast<int>(bodyBytes.size()));
    frame.insert(frame.end(), bodyBytes.begin(), bodyBytes.end());

    return frame;
}

void TestRequestWhitelist()
{
    // Only the tournament plane moves. Live games, the user's own matches and the
    // local rank data stay with the local GC, which actually has them.
    Check(WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames),
        "9146 is forwarded");
    Check(WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions),
        "9160 is forwarded");
    Check(WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestFullGameInfo),
        "9147 is forwarded");

    Check(!WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestCurrentLiveGames),
        "9140 stays local");
    Check(!WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestRecentUserGames),
        "9141 stays local");
    Check(!WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestLiveGameForUser),
        "9154 stays local");
    Check(!WatchPassthrough::Forwards(k_EMsgGCClientHello), "the hello is not a forwarded request");
    Check(!WatchPassthrough::Forwards(k_EMsgGCClientWelcome), "4004 is not forwarded");
    Check(!WatchPassthrough::Forwards(k_ESOMsg_CacheSubscribed), "socache traffic stays local");

    // The mask must not change the answer: the game masks every protobuf type.
    Check(WatchPassthrough::Forwards(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames | ProtobufMask),
        "masked 9146 is forwarded");
}

void TestHelloDetection()
{
    Check(WatchPassthrough::IsHello(k_EMsgGCClientHello), "4006 is a hello");
    Check(WatchPassthrough::IsHello(k_EMsgGCClientHelloPartner), "4011 is a hello");
    Check(WatchPassthrough::IsHello(k_EMsgGCClientHelloPW), "4012 is a hello");
    Check(WatchPassthrough::IsHello(k_EMsgGCClientHelloR2), "4013 is a hello");
    Check(WatchPassthrough::IsHello(k_EMsgGCClientHelloR3), "4014 is a hello");
    Check(WatchPassthrough::IsHello(k_EMsgGCClientHelloR4), "4015 is a hello");
    Check(!WatchPassthrough::IsHello(k_EMsgGCClientWelcome), "4004 is not a hello");
    Check(!WatchPassthrough::IsHello(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames),
        "9146 is not a hello");
}

void TestHelloVersionRewrite()
{
    // The config fixture sets hello_version 9999.
    Check(WatchPassthrough::HelloVersionOverride() == 9999, "hello_version comes from config");

    CMsgClientHello hello;
    hello.set_version(1575);
    hello.set_client_session_need(0);
    const std::vector<uint8_t> frame = Frame(k_EMsgGCClientHello, hello);

    g_upstream.sentTypes.clear();
    g_upstream.sentBodies.clear();

    Check(WatchPassthrough::Forward(k_EMsgGCClientHello, frame.data(),
              static_cast<uint32_t>(frame.size())),
        "forwarding a hello succeeds");

    Check(g_upstream.sentBodies.size() == 1, "the hello reached the real GC");
    if (g_upstream.sentBodies.size() == 1)
    {
        const std::vector<uint8_t> &sent = g_upstream.sentBodies.front();

        // The masked type and the header must survive verbatim, or the real GC
        // rejects the message and the job id stops matching.
        Check(sent.size() >= frame.size(), "the rewritten frame did not shrink");
        Check(memcmp(sent.data(), frame.data(), sizeof(uint32_t) * 2) == 0,
            "the message type and header length are unchanged");

        const CMsgClientHello parsed = [&] {
            CMsgClientHello result;
            uint32_t headerSize = 0;
            memcpy(&headerSize, sent.data() + sizeof(uint32_t), sizeof(headerSize));
            result.ParseFromArray(sent.data() + sizeof(uint32_t) * 2 + headerSize,
                static_cast<int>(sent.size() - sizeof(uint32_t) * 2 - headerSize));
            return result;
        }();

        Check(parsed.version() == 9999, "the advertised version was rewritten");
        Check(parsed.client_session_need() == 0, "the rest of the hello is preserved");
    }
}

void TestForwardSkipsRewriteOutsideHello()
{
    g_upstream.sentBodies.clear();

    CMsgGCCStrike15_v2_MatchListRequestTournamentGames request;
    request.set_eventid(20);
    const std::vector<uint8_t> frame = Frame(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames, request);

    Check(WatchPassthrough::Forward(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames,
              frame.data(), static_cast<uint32_t>(frame.size())),
        "forwarding a tournament request succeeds");

    Check(g_upstream.sentBodies.size() == 1, "the request reached the real GC");
    if (g_upstream.sentBodies.size() == 1)
    {
        Check(g_upstream.sentBodies.front() == frame, "a non-hello message is forwarded byte for byte");
    }
}

void TestPumpKeepsTournamentReplies()
{
    InstallFakeUpstream();

    CMsgGCCStrike15_v2_MatchList matchList;
    matchList.set_msgrequestid(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames);
    matchList.set_servertime(1);

    CMsgClientWelcome welcome;
    welcome.set_version(1575);

    CMsgGCCStrike15_v2_GC2ClientTournamentInfo tournamentInfo;
    tournamentInfo.set_eventid(20);

    g_upstream.Queue(k_EMsgGCCStrike15_v2_MatchList | ProtobufMask, Frame(k_EMsgGCCStrike15_v2_MatchList, matchList));
    g_upstream.Queue(k_EMsgGCClientWelcome | ProtobufMask, Frame(k_EMsgGCClientWelcome, welcome));
    g_upstream.Queue(k_EMsgGCCStrike15_v2_GC2ClientTournamentInfo | ProtobufMask,
        Frame(k_EMsgGCCStrike15_v2_GC2ClientTournamentInfo, tournamentInfo));

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> keep;
    std::vector<uint32_t> droppedTypes;
    WatchPassthrough::Pump(keep, droppedTypes);

    Check(keep.size() == 2, "only the two tournament replies are kept");
    Check(droppedTypes.size() == 1, "the welcome is dropped");
    if (droppedTypes.size() == 1)
    {
        Check(droppedTypes.front() == (k_EMsgGCClientWelcome | ProtobufMask),
            "the dropped type is the welcome");
    }
    Check(g_upstream.queued.empty(), "the upstream queue was drained");

    if (keep.size() == 2)
    {
        // The queue type stays masked, because that is what the local GC already
        // publishes and what the game compares against.
        Check(keep[0].first == (k_EMsgGCCStrike15_v2_MatchList | ProtobufMask), "9139 stays masked");
        Check(keep[1].first == (k_EMsgGCCStrike15_v2_GC2ClientTournamentInfo | ProtobufMask),
            "9167 stays masked");

        CMsgGCCStrike15_v2_MatchList parsed;
        uint32_t headerSize = 0;
        memcpy(&headerSize, keep[0].second.data() + sizeof(uint32_t), sizeof(headerSize));
        const size_t bodyOffset = sizeof(uint32_t) * 2 + headerSize;
        Check(parsed.ParseFromArray(keep[0].second.data() + bodyOffset,
                  static_cast<int>(keep[0].second.size() - bodyOffset)),
            "the kept 9139 still parses");
        Check(parsed.msgrequestid() == k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames,
            "the kept 9139 kept its contents");
    }
}

void TestPumpGrowsForLargeFrames()
{
    InstallFakeUpstream();

    // Bigger than the fixed initial buffer Pump starts with, so this only passes
    // when it honours the buffer-too-small report and retries.
    CMsgGCCStrike15_v2_MatchList matchList;
    for (int i = 0; i < 20000; i++)
    {
        matchList.add_streams()->set_team_name("a fairly long team name for padding");
    }

    g_upstream.Queue(k_EMsgGCCStrike15_v2_MatchList | ProtobufMask,
        Frame(k_EMsgGCCStrike15_v2_MatchList, matchList));

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> keep;
    std::vector<uint32_t> droppedTypes;
    WatchPassthrough::Pump(keep, droppedTypes);

    Check(keep.size() == 1, "an oversized reply is still delivered");
    Check(droppedTypes.empty(), "nothing was dropped");
}

void TestDetach()
{
    WatchPassthrough::DetachAll();

    CMsgClientHello hello;
    const std::vector<uint8_t> frame = Frame(k_EMsgGCClientHello, hello);

    Check(!WatchPassthrough::Forward(k_EMsgGCClientHello, frame.data(),
              static_cast<uint32_t>(frame.size())),
        "a detached upstream is not forwarded to");
}

} // namespace

int main(int argc, char **argv)
{
    const bool runConfigCases = argc > 1 && !strcmp(argv[1], "config");

    if (runConfigCases)
    {
        // The configured hello version is the whole point of the option, so a
        // missing or malformed config has to fall back rather than guess.
        Check(WatchPassthrough::HelloVersionOverride() == 0, "no config means no override");
        Check(!WatchPassthrough::IsEnabled(), "no config means the mode stays off");
    }
    else
    {
        Check(WatchPassthrough::IsEnabled(), "the mode is on in this fixture");
        InstallFakeUpstream();
        TestRequestWhitelist();
        TestHelloDetection();
        TestHelloVersionRewrite();
        TestForwardSkipsRewriteOutsideHello();
        TestPumpKeepsTournamentReplies();
        TestPumpGrowsForLargeFrames();
        TestDetach();
    }

    if (g_failures)
    {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }

    printf("watch_passthrough_tests: ok\n");
    return 0;
}
