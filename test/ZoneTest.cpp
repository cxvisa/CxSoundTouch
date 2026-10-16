#include "SpeakerConfig.h"
#include "SoundTouchClient.h"
#include "ZoneManager.h"
#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

static int g_checks = 0;
static int g_failures = 0;

static void check (bool condition, const char *expression, const char *file, int line)
{
    ++g_checks;

    if (!condition)
    {
        ++g_failures;
        std::cerr << "FAIL " << file << ":" << line << ": " << expression << "\n";
    }
}

#define CHECK(cond) check ((cond), #cond, __FILE__, __LINE__)

static SoundTouchDevice makeDev (const std::string &id, const std::string &name, const std::string &ip)
{
    SoundTouchDevice d;
    d.deviceId = id;
    d.deviceName = name;
    d.ipAddress = ip;
    d.deviceType = "SoundTouch 30";
    return (d);
}

// -----------------------------------------------------------------------------
// SpeakerConfig Zone and Target Tests
// -----------------------------------------------------------------------------

static void testSpeakerConfigZonesAndTargets ()
{
    const std::string jsonText = R"({
  "default_device": "DEV_AAA",
  "devices": [
    { "device_id": "DEV_AAA", "device_name": "Living Room", "ip_address": "192.168.1.10", "custom_prop": 123 },
    { "device_id": "DEV_BBB", "device_name": "Kitchen", "ip_address": "192.168.1.11" },
    { "device_id": "DEV_CCC", "device_name": "Bedroom", "ip_address": "192.168.1.12" }
  ],
  "zones": [
    {
      "id": "downstairs",
      "name": "Downstairs Zone",
      "leader_id": "DEV_AAA",
      "members": [ "DEV_AAA", "DEV_BBB" ],
      "zone_note": "evening preset"
    }
  ],
  "default_target": {
    "type": "zone",
    "id": "downstairs"
  },
  "root_extra": "preserved"
})";

    SpeakerConfig config;
    CHECK (config.loadFromText (jsonText));

    CHECK (config.getSpeakers ().size () == 3);
    CHECK (config.getZones ().size () == 1);
    CHECK (config.hasDefaultTarget ());
    CHECK (config.getDefaultTarget ().type == PlaybackTarget::Type::Zone);
    CHECK (config.getDefaultTarget ().id == "downstairs");

    // Leader speaker of the default zone is returned by getDefaultSpeaker
    const SoundTouchDevice *defSp = config.getDefaultSpeaker ();
    CHECK (defSp != nullptr);
    CHECK (defSp->deviceId == "DEV_AAA");

    // Zone lookup
    const ZoneConfig *z = config.findZoneById ("downstairs");
    CHECK (z != nullptr);
    CHECK (z->name == "Downstairs Zone");
    CHECK (z->leaderId == "DEV_AAA");
    CHECK (z->memberIds.size () == 2);
    CHECK (config.findZoneByName ("Downstairs Zone") == z);
    CHECK (config.findZoneByName ("Nonexistent") == nullptr);

    // Resolve default target
    ResolvedTarget resolved;
    CHECK (config.resolveDefaultTarget (resolved));
    CHECK (resolved.isZone == true);
    CHECK (resolved.autoResume == false);
    CHECK (resolved.leaderDeviceId == "DEV_AAA");
    CHECK (resolved.leaderIp == "192.168.1.10");
    CHECK (resolved.zoneId == "downstairs");
    CHECK (resolved.zoneName == "Downstairs Zone");
    CHECK (resolved.memberDeviceIds.size () == 2);
    CHECK (resolved.memberDeviceIds[0] == "DEV_AAA");
    CHECK (resolved.memberDeviceIds[1] == "DEV_BBB");
    CHECK (resolved.memberIps[0] == "192.168.1.10");
    CHECK (resolved.memberIps[1] == "192.168.1.11");

    // Resolve solo target
    PlaybackTarget soloTarget { PlaybackTarget::Type::Speaker, "DEV_CCC" };
    ResolvedTarget soloResolved;
    std::string err;
    CHECK (config.resolveTarget (soloTarget, soloResolved, err));
    CHECK (soloResolved.isZone == false);
    CHECK (soloResolved.autoResume == true);
    CHECK (soloResolved.leaderDeviceId == "DEV_CCC");
    CHECK (soloResolved.leaderIp == "192.168.1.12");
    CHECK (soloResolved.memberDeviceIds.size () == 1);

    // Preservation of unknown fields on round-trip fileText
    const std::string written = config.fileText (jsonText);
    nlohmann::json doc = nlohmann::json::parse (written);

    CHECK (doc["root_extra"] == "preserved");
    CHECK (doc["devices"][0]["custom_prop"] == 123);
    CHECK (doc["zones"][0]["zone_note"] == "evening preset");
    CHECK (doc["default_target"]["type"] == "zone");
    CHECK (doc["default_target"]["id"] == "downstairs");
    CHECK (doc["default_device"] == "DEV_AAA");
}

static void testLegacyDefaultDeviceCompatibility ()
{
    // Legacy devices.json with only default_device and no default_target
    const std::string legacy = R"({
  "default_device": "DEV_BBB",
  "devices": [
    { "device_id": "DEV_AAA", "device_name": "Living", "ip_address": "10.0.0.1" },
    { "device_id": "DEV_BBB", "device_name": "Kitchen", "ip_address": "10.0.0.2" }
  ]
})";

    SpeakerConfig config;
    CHECK (config.loadFromText (legacy));
    CHECK (!config.hasDefaultTarget ());
    CHECK (config.getDefaultId () == "DEV_BBB");

    ResolvedTarget resolved;
    CHECK (config.resolveDefaultTarget (resolved));
    CHECK (resolved.isZone == false);
    CHECK (resolved.autoResume == true);
    CHECK (resolved.leaderDeviceId == "DEV_BBB");
    CHECK (resolved.leaderIp == "10.0.0.2");

    // fileText on legacy without zones or default_target does not invent them
    const std::string out = config.fileText (legacy);
    nlohmann::json doc = nlohmann::json::parse (out);
    CHECK (doc.contains ("default_device") && doc["default_device"] == "DEV_BBB");
    CHECK (!doc.contains ("zones"));
    CHECK (!doc.contains ("default_target"));
}

static void testZoneValidationRules ()
{
    SpeakerConfig config;
    config.remember (makeDev ("AAA", "Speaker A", "10.0.0.1"));
    config.remember (makeDev ("BBB", "Speaker B", "10.0.0.2"));
    config.remember (makeDev ("CCC", "Speaker C", "10.0.0.3"));

    std::string err;

    // 1. Empty ID rejected
    ZoneConfig z1 { "", "Zone 1", "AAA", { "AAA", "BBB" } };
    CHECK (!config.validateZone (z1, err));
    CHECK (!err.empty ());

    // 2. Empty Name rejected
    ZoneConfig z2 { "z2", "   ", "AAA", { "AAA", "BBB" } };
    CHECK (!config.validateZone (z2, err));

    // 3. Unknown leader rejected
    ZoneConfig z3 { "z3", "Zone 3", "UNKNOWN", { "UNKNOWN", "BBB" } };
    CHECK (!config.validateZone (z3, err));

    // 4. Less than 2 members rejected
    ZoneConfig z4 { "z4", "Zone 4", "AAA", { "AAA" } };
    CHECK (!config.validateZone (z4, err));

    // 5. Leader missing from member list rejected
    ZoneConfig z5 { "z5", "Zone 5", "AAA", { "BBB", "CCC" } };
    CHECK (!config.validateZone (z5, err));

    // 6. Unknown member speaker rejected
    ZoneConfig z6 { "z6", "Zone 6", "AAA", { "AAA", "UNKNOWN" } };
    CHECK (!config.validateZone (z6, err));

    // 7. Duplicate member IDs rejected
    ZoneConfig z7 { "z7", "Zone 7", "AAA", { "AAA", "BBB", "BBB" } };
    CHECK (!config.validateZone (z7, err));

    // 8. Valid zone accepted
    ZoneConfig valid { "z_main", "Main Floor", "AAA", { "AAA", "BBB" } };
    CHECK (config.validateZone (valid, err));
    CHECK (config.addZone (valid, err));
    CHECK (config.getZones ().size () == 1);

    // 9. Duplicate zone ID rejected
    ZoneConfig dupId { "z_main", "Other Name", "AAA", { "AAA", "CCC" } };
    CHECK (!config.addZone (dupId, err));

    // 10. Duplicate zone name rejected
    ZoneConfig dupName { "z_other", "Main Floor", "AAA", { "AAA", "CCC" } };
    CHECK (!config.addZone (dupName, err));

    // 11. Update zone
    ZoneConfig updated { "z_main", "Main Floor Renamed", "AAA", { "AAA", "BBB", "CCC" } };
    CHECK (config.updateZone (updated, err));
    CHECK (config.findZoneById ("z_main")->name == "Main Floor Renamed");
    CHECK (config.findZoneById ("z_main")->memberIds.size () == 3);

    // Test loadFromText rejects invalid zone with unknown speaker
    const std::string invalidJson = R"({
  "devices": [
    { "device_id": "AAA", "device_name": "Speaker A", "ip_address": "10.0.0.1" }
  ],
  "zones": [
    { "id": "test-pair", "name": "Test Pair", "leader_id": "AAA", "members": ["AAA", "UNKNOWN"] }
  ]
})";
    SpeakerConfig invalidConfig;
    CHECK (!invalidConfig.loadFromText (invalidJson));

    // 12. Remove zone
    CHECK (config.removeZone ("z_main", err));
    CHECK (config.getZones ().empty ());
    CHECK (!config.removeZone ("z_main", err));
}

static void testTargetJsonHelpers ()
{
    PlaybackTarget target;
    std::string err;

    CHECK (SpeakerConfig::parseTargetJson (R"({"type":"speaker","id":"AAA"})", target, err));
    CHECK (target.type == PlaybackTarget::Type::Speaker);
    CHECK (target.id == "AAA");

    CHECK (SpeakerConfig::parseTargetJson (R"({"type":"zone","id":"party"})", target, err));
    CHECK (target.type == PlaybackTarget::Type::Zone);
    CHECK (target.id == "party");

    CHECK (!SpeakerConfig::parseTargetJson (R"({"type":"invalid","id":"AAA"})", target, err));
    CHECK (!SpeakerConfig::parseTargetJson (R"({"id":"AAA"})", target, err));
    CHECK (!SpeakerConfig::parseTargetJson (R"({"type":"speaker"})", target, err));
    CHECK (!SpeakerConfig::parseTargetJson ("not json", target, err));

    PlaybackTarget outT { PlaybackTarget::Type::Zone, "living_room" };
    const std::string jsonOut = SpeakerConfig::targetToJson (outT);
    nlohmann::json j = nlohmann::json::parse (jsonOut);
    CHECK (j["type"] == "zone");
    CHECK (j["id"] == "living_room");
}

// -----------------------------------------------------------------------------
// SoundTouchClient XML Generation and Normalization Tests
// -----------------------------------------------------------------------------

static void testBuildSetZoneXml ()
{
    std::vector<SoundTouchClient::ZoneMember> followers;
    followers.push_back ({ "DEV_BBB", "192.168.1.11", 0, false });
    followers.push_back ({ "DEV_CCC", "192.168.1.12", 0, false });

    const std::string xml = SoundTouchClient::buildSetZoneXml ("DEV_AAA", "192.168.1.100", followers);

    CHECK (xml.find ("<zone master=\"DEV_AAA\" senderIPAddress=\"192.168.1.100\">") != std::string::npos);
    CHECK (xml.find ("<member ipaddress=\"192.168.1.11\">DEV_BBB</member>") != std::string::npos);
    CHECK (xml.find ("<member ipaddress=\"192.168.1.12\">DEV_CCC</member>") != std::string::npos);

    // If master is inadvertently passed in followers list, it must be excluded from follower tags
    followers.push_back ({ "DEV_AAA", "192.168.1.10", 0, true });
    const std::string xmlFiltered = SoundTouchClient::buildSetZoneXml ("DEV_AAA", "192.168.1.100", followers);
    CHECK (xmlFiltered.find ("<member ipaddress=\"192.168.1.10\">DEV_AAA</member>") == std::string::npos);
}

static void testBuildZoneSlaveXml ()
{
    SoundTouchClient::ZoneMember follower { "DEV_BBB", "192.168.1.11", 0, false };
    const std::string xml = SoundTouchClient::buildZoneSlaveXml ("DEV_AAA", follower);

    CHECK (xml.find ("<zone master=\"DEV_AAA\">") != std::string::npos);
    CHECK (xml.find ("<member ipaddress=\"192.168.1.11\">DEV_BBB</member>") != std::string::npos);
}

static void testParseZoneXmlLeaderExcludedNormalization ()
{
    // Actual tested Bose behavior: /getZone often excludes leader from member list
    const std::string boseXml = R"(
<zone master="DEV_AAA" senderIPAddress="192.168.1.10">
  <member ipaddress="192.168.1.11">DEV_BBB</member>
  <member ipaddress="192.168.1.12">DEV_CCC</member>
</zone>
)";

    SoundTouchClient::ZoneStatus status;
    std::string err;
    CHECK (SoundTouchClient::parseZoneXml (boseXml, "192.168.1.10", status, &err));

    CHECK (status.inZone == true);
    CHECK (status.masterId == "DEV_AAA");
    CHECK (status.senderIpAddress == "192.168.1.10");
    CHECK (status.isLeader == true);

    // Leader was normalized into members list
    CHECK (status.members.size () == 3);
    CHECK (status.members[0].deviceId == "DEV_AAA");
    CHECK (status.members[0].ipAddress == "192.168.1.10");
    CHECK (status.members[0].isLeader == true);

    CHECK (status.members[1].deviceId == "DEV_BBB");
    CHECK (status.members[1].isLeader == false);
    CHECK (status.members[2].deviceId == "DEV_CCC");
    CHECK (status.members[2].isLeader == false);
}

static void testParseZoneXmlLeaderIncludedNoDuplicate ()
{
    const std::string xmlWithLeader = R"(
<zone master="DEV_AAA" senderIPAddress="192.168.1.10">
  <member ipaddress="192.168.1.10" role="leader">DEV_AAA</member>
  <member ipaddress="192.168.1.11">DEV_BBB</member>
</zone>
)";

    SoundTouchClient::ZoneStatus status;
    CHECK (SoundTouchClient::parseZoneXml (xmlWithLeader, "192.168.1.10", status));

    CHECK (status.inZone == true);
    CHECK (status.members.size () == 2);
    CHECK (status.members[0].deviceId == "DEV_AAA");
    CHECK (status.members[0].isLeader == true);
    CHECK (status.members[1].deviceId == "DEV_BBB");
    CHECK (status.members[1].isLeader == false);
}

static void testParseZoneXmlUngrouped ()
{
    SoundTouchClient::ZoneStatus status1;
    CHECK (SoundTouchClient::parseZoneXml ("<zone/>", "192.168.1.10", status1));
    CHECK (status1.inZone == false);
    CHECK (status1.members.empty ());

    SoundTouchClient::ZoneStatus status2;
    CHECK (SoundTouchClient::parseZoneXml ("<zone master=\"\"/>", "192.168.1.10", status2));
    CHECK (status2.inZone == false);
    CHECK (status2.members.empty ());
}

static void testParseBoseErrorsXml ()
{
    const std::string errorXml = R"(
<errors>
  <error value="1054" name="ZONE_ERROR">Invalid master device</error>
</errors>
)";

    std::string err;
    CHECK (SoundTouchClient::parseBoseErrors (errorXml, err));
    CHECK (err.find ("1054") != std::string::npos);
    CHECK (err.find ("ZONE_ERROR") != std::string::npos);
    CHECK (err.find ("Invalid master device") != std::string::npos);

    SoundTouchClient::ZoneStatus status;
    std::string parseErr;
    CHECK (!SoundTouchClient::parseZoneXml (errorXml, "192.168.1.10", status, &parseErr));
    CHECK (!parseErr.empty ());
}

static void testLocalRouteIpDiscovery ()
{
    // Verification of POSIX UDP getsockname discovery (network-free local kernel query)
    SoundTouchClient client ("127.0.0.1");
    const std::string localIp = client.getLocalRouteIp ();
    CHECK (!localIp.empty ());
    CHECK (localIp == "127.0.0.1");

    SoundTouchClient emptyClient ("");
    CHECK (emptyClient.getLocalRouteIp () == "");
}

// -----------------------------------------------------------------------------
// ZoneManager Preflight & Topological Error Tests
// -----------------------------------------------------------------------------

static void testZoneManagerPreflightErrors ()
{
    SpeakerConfig config;
    config.remember (makeDev ("AAA", "Speaker A", "127.0.0.1"));
    config.remember (makeDev ("BBB", "Speaker B", "127.0.0.1"));

    ZoneManager manager (config);

    // Unconfigured target should return clean preflight failure
    PlaybackTarget nonExistent { PlaybackTarget::Type::Zone, "ghost_zone" };
    ZoneManager::PreflightReport report = manager.preflight (nonExistent);
    CHECK (report.ready == false);
    CHECK (report.message.find ("not found") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------


static void testZoneSnapshotStructure ()
{
    SpeakerConfig config;
    config.remember (makeDev ("AAA", "Speaker A", "127.0.0.1"));
    config.remember (makeDev ("BBB", "Speaker B", "127.0.0.1"));

    ZoneManager manager (config);
    ZoneManager::ZoneSnapshot snap = manager.snapshot (100);
    CHECK (snap.devices.size () == 2);
    CHECK (snap.devices[0].deviceId == "AAA");
    CHECK (snap.devices[1].deviceId == "BBB");
}

static void testLiveMembershipMatches ()
{
    using State = ZoneManager::DeviceLiveState;
    const std::vector<std::string> expected { "A", "B", "C" };
    State leader;   leader.deviceId = "A"; leader.inZone = true; leader.liveMasterId = "A"; leader.liveFollowerIds = { "C", "B" };
    State followB;  followB.deviceId = "B"; followB.inZone = true; followB.liveMasterId = "A"; followB.liveFollowerIds = { "B" };
    State followC;  followC.deviceId = "C"; followC.inZone = true; followC.liveMasterId = "A"; followC.liveFollowerIds = { "C" };
    CHECK (ZoneManager::liveMembershipMatches ({ followB, leader, followC }, expected, "A"));

    State extra = leader; extra.liveFollowerIds = { "B", "C", "D" };
    CHECK (!ZoneManager::liveMembershipMatches ({ extra, followB, followC }, expected, "A"));
    State missing = leader; missing.liveFollowerIds = { "B" };
    CHECK (!ZoneManager::liveMembershipMatches ({ missing, followB }, expected, "A"));
    State moved = followC; moved.liveMasterId = "X";
    CHECK (!ZoneManager::liveMembershipMatches ({ leader, followB, moved }, expected, "A"));
}

static void testNowPlayingMetadata ()
{
    SoundTouchClient::NowPlaying now;
    CHECK (SoundTouchClient::parseNowPlayingXml (
        R"(<nowPlaying source="AIRPLAY"><track>Song - Part Two &amp; More</track><artist>Artist &lt;One&gt;</artist><album>Album</album><playStatus>PLAY_STATE</playStatus></nowPlaying>)", now));
    CHECK (now.source == "AIRPLAY");
    CHECK (now.track == "Song - Part Two & More");
    CHECK (now.artist == "Artist <One>");
    CHECK (now.album == "Album");
    CHECK (now.status == "PLAY_STATE");

    CHECK (SoundTouchClient::parseNowPlayingXml (
        R"(<updates><nowPlayingUpdated><nowPlaying source="AIRPLAY"><track>Next song</track><artist>Next artist</artist><playStatus>PAUSE_STATE</playStatus></nowPlaying></nowPlayingUpdated></updates>)", now));
    CHECK (now.track == "Next song");
    CHECK (now.artist == "Next artist");
    CHECK (now.album.empty ());
    CHECK (now.status == "PAUSE_STATE");

    CHECK (SoundTouchClient::parseNowPlayingXml (R"(<nowPlaying source="AIRPLAY"/>)", now));
    CHECK (now.track.empty () && now.artist.empty () && now.album.empty ());
    CHECK (SoundTouchClient::parseNowPlayingXml (
        R"(<nowPlaying source="UPNP"><ContentItem location="http://host/stream"/><playStatus>PLAY_STATE</playStatus></nowPlaying>)", now));
    CHECK (now.source == "UPNP" && now.location == "http://host/stream");
    CHECK (now.track.empty () && now.artist.empty ());
    CHECK (!SoundTouchClient::parseNowPlayingXml ("<broken", now));
    CHECK (now.source.empty ());
    CHECK (!SoundTouchClient::parseNowPlayingXml ("<updates/>", now));
}

int main ()
{
    testNowPlayingMetadata ();
    testLiveMembershipMatches ();
    testSpeakerConfigZonesAndTargets ();
    testLegacyDefaultDeviceCompatibility ();
    testZoneValidationRules ();
    testTargetJsonHelpers ();

    testBuildSetZoneXml ();
    testBuildZoneSlaveXml ();
    testParseZoneXmlLeaderExcludedNormalization ();
    testParseZoneXmlLeaderIncludedNoDuplicate ();
    testParseZoneXmlUngrouped ();
    testParseBoseErrorsXml ();
    testLocalRouteIpDiscovery ();

    testZoneManagerPreflightErrors ();
    testZoneSnapshotStructure ();

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return (1);
    }

    std::cout << "All zone unit tests passed.\n";

    return (0);
}
