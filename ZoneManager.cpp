#include "ZoneManager.h"
#include <algorithm>
#include <set>
#include <thread>
#include <chrono>

ZoneManager::ZoneManager (const SpeakerConfig &config)
    : m_config (config)
{
}

bool ZoneManager::liveMembershipMatches (const std::vector<DeviceLiveState> &devices,
                                         const std::vector<std::string> &expectedMemberIds,
                                         const std::string &leaderDeviceId)
{
    std::set<std::string> expected (expectedMemberIds.begin (), expectedMemberIds.end ());
    for (const auto &device : devices)
    {
        if (!device.inZone) continue;
        if (device.liveMasterId != leaderDeviceId) return (false);
        if (device.deviceId != leaderDeviceId) continue;
        std::set<std::string> actual (device.liveFollowerIds.begin (), device.liveFollowerIds.end ());
        actual.insert (device.liveMasterId);
        if (actual != expected) return (false);
    }
    return (true);
}

ZoneManager::PreflightReport ZoneManager::preflight (const PlaybackTarget &target, bool takeover, long timeoutMs)
{
    PreflightReport report;
    ResolvedTarget resolved;
    std::string resolveErr;

    if (!m_config.resolveTarget (target, resolved, resolveErr))
    {
        report.ready = false;
        report.conflict = false;
        report.message = resolveErr;
        return (report);
    }

    bool allOnline = true;
    bool hasConflict = false;
    std::string conflictMsg;

    std::set<std::string> expectedFollowers;
    for (const std::string &mId : resolved.memberDeviceIds)
    {
        if (mId != resolved.leaderDeviceId)
        {
            expectedFollowers.insert (mId);
        }
    }

    for (const std::string &memberId : resolved.memberDeviceIds)
    {
        const SoundTouchDevice *dev = m_config.findById (memberId);

        if (dev == nullptr)
        {
            allOnline = false;
            report.message = "Configured speaker '" + memberId + "' not found in configuration";
            report.ready = false;
            return (report);
        }

        DeviceLiveState state;
        state.deviceId = dev->deviceId;
        state.ipAddress = dev->ipAddress;
        state.deviceName = dev->deviceName;

        SoundTouchClient client (dev->ipAddress);
        SoundTouchClient::ZoneStatus zStatus;
        std::string devErr;

        if (!client.getZone (zStatus, timeoutMs, &devErr))
        {
            state.online = false;
            state.error = devErr;
            allOnline = false;
            report.message = "Speaker '" + dev->deviceName + "' (" + dev->ipAddress + ") is offline: " + devErr;
        }
        else
        {
            state.online = true;
            state.inZone = zStatus.inZone;
            state.isLeader = zStatus.isLeader;
            state.liveMasterId = zStatus.masterId;

            for (const auto &m : zStatus.members)
            {
                if (m.deviceId != zStatus.masterId)
                {
                    state.liveFollowerIds.push_back (m.deviceId);
                }
            }

            if (zStatus.inZone)
            {
                if (target.type == PlaybackTarget::Type::Zone)
                {
                    if (memberId == resolved.leaderDeviceId)
                    {
                        if (zStatus.masterId != resolved.leaderDeviceId)
                        {
                            hasConflict = true;
                            conflictMsg = "Leader speaker '" + dev->deviceName + "' is currently a follower in another zone (leader: " + zStatus.masterId + ")";
                        }
                        else
                        {
                            // Leader is master of a live zone; check if actual follower set matches exactly
                            std::set<std::string> liveFollowers (state.liveFollowerIds.begin (), state.liveFollowerIds.end ());
                            if (liveFollowers != expectedFollowers)
                            {
                                hasConflict = true;
                                conflictMsg = "Leader speaker '" + dev->deviceName + "' has active followers that differ from requested zone";
                            }
                        }
                    }
                    else
                    {
                        if (zStatus.masterId != resolved.leaderDeviceId)
                        {
                            hasConflict = true;
                            conflictMsg = "Member speaker '" + dev->deviceName + "' is in another live zone (leader: " + zStatus.masterId + ")";
                        }
                    }
                }
                else
                {
                    hasConflict = true;
                    conflictMsg = "Speaker '" + dev->deviceName + "' is currently in a live zone (leader: " + zStatus.masterId + ")";
                }
            }

            // Check if prospective follower is playing unrelated audio
            if (target.type == PlaybackTarget::Type::Zone && memberId != resolved.leaderDeviceId)
            {
                bool alreadyInTargetZone = (zStatus.inZone && zStatus.masterId == resolved.leaderDeviceId);
                if (!alreadyInTargetZone)
                {
                    SoundTouchClient::NowPlaying now = client.glance (timeoutMs);
                    if (now.source.empty () && now.status.empty ())
                    {
                        // Could not query playback state
                        hasConflict = true;
                        conflictMsg = "Could not query playback state for follower speaker '" + dev->deviceName + "'";
                    }
                    else if (now.source != "STANDBY" && now.source != "INVALID_SOURCE" &&
                             (now.status == "PLAY_STATE" || now.status == "BUFFERING_STATE"))
                    {
                        hasConflict = true;
                        conflictMsg = "Follower speaker '" + dev->deviceName + "' is actively playing audio (source: " + now.source + ", status: " + now.status + ")";
                    }
                }
            }
        }

        report.devices.push_back (state);
    }

    report.conflict = hasConflict;

    if (!allOnline)
    {
        report.ready = false;
        return (report);
    }

    if (hasConflict && !takeover)
    {
        report.ready = false;
        report.message = conflictMsg + " (takeover required)";
        return (report);
    }

    report.ready = true;
    report.message = "Target ready";

    return (report);
}

bool ZoneManager::activate (const PlaybackTarget &target, bool takeover, std::string &error, long timeoutMs)
{
    ResolvedTarget resolved;

    if (!m_config.resolveTarget (target, resolved, error))
    {
        return (false);
    }

    std::vector<SoundTouchClient::ZoneStatus> currentStatuses;

    for (size_t i = 0; i < resolved.memberDeviceIds.size (); ++i)
    {
        const std::string &memberId = resolved.memberDeviceIds[i];
        const std::string &memberIp = resolved.memberIps[i];
        SoundTouchClient client (memberIp);
        SoundTouchClient::ZoneStatus curStatus;
        std::string devErr;

        if (!client.getZone (curStatus, timeoutMs / 2, &devErr))
        {
            const SoundTouchDevice *dev = m_config.findById (memberId);
            const std::string name = (dev != nullptr ? dev->deviceName : memberId);

            error = "Activation failed: speaker '" + name + "' (" + memberIp + ") is offline or unreachable: " + devErr;
            return (false);
        }

        currentStatuses.push_back (curStatus);
    }

    if (target.type == PlaybackTarget::Type::Speaker)
    {
        const SoundTouchClient::ZoneStatus &curStatus = currentStatuses[0];

        if (!curStatus.inZone)
        {
            // Already a solo speaker, nothing to do
            return (true);
        }

        if (!takeover)
        {
            error = "Speaker '" + resolved.leaderDeviceName + "' is in a live zone (leader: " + curStatus.masterId + "); takeover required";
            return (false);
        }

        if (curStatus.masterId == resolved.leaderDeviceId)
        {
            std::string disErr;

            if (!dissolve (resolved.leaderDeviceId, disErr, timeoutMs))
            {
                error = "Failed to dissolve existing zone on '" + resolved.leaderDeviceName + "' for solo activation: " + disErr;
                return (false);
            }
        }
        else
        {
            const SoundTouchDevice *oldMaster = m_config.findById (curStatus.masterId);

            if (oldMaster == nullptr)
            {
                error = "Cannot remove speaker '" + resolved.leaderDeviceName + "' from unknown leader '" + curStatus.masterId + "'";
                return (false);
            }

            SoundTouchClient masterClient (oldMaster->ipAddress);
            SoundTouchClient::ZoneMember follower;

            follower.deviceId = resolved.leaderDeviceId;
            follower.ipAddress = resolved.leaderIp;

            std::string remErr;

            if (!masterClient.removeZoneSlave (curStatus.masterId, follower, timeoutMs / 2, &remErr))
            {
                error = "Failed to remove speaker '" + resolved.leaderDeviceName + "' from zone leader '" + oldMaster->deviceName + "': " + remErr;
                return (false);
            }
        }

        SoundTouchClient client (resolved.leaderIp);
        SoundTouchClient::ZoneStatus verifyStatus;
        std::string verifyErr;

        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        if (!client.getZone (verifyStatus, timeoutMs / 2, &verifyErr))
        {
            error = "Failed to verify solo state for speaker '" + resolved.leaderDeviceName + "': " + verifyErr;
            return (false);
        }

        if (verifyStatus.inZone)
        {
            error = "Speaker '" + resolved.leaderDeviceName + "' is still in a live zone after removal attempt";
            return (false);
        }

        return (true);
    }

    // Zone target activation
    const SoundTouchClient::ZoneStatus &leaderStatus = currentStatuses[0];
    std::set<std::string> expectedFollowers;
    std::vector<SoundTouchClient::ZoneMember> followers;

    for (size_t i = 0; i < resolved.memberDeviceIds.size (); ++i)
    {
        if (resolved.memberDeviceIds[i] == resolved.leaderDeviceId)
        {
            continue;
        }

        expectedFollowers.insert (resolved.memberDeviceIds[i]);

        SoundTouchClient::ZoneMember m;
        m.deviceId = resolved.memberDeviceIds[i];
        m.ipAddress = resolved.memberIps[i];
        followers.push_back (m);
    }

    // Check if exact topology is already active
    if (leaderStatus.inZone && leaderStatus.masterId == resolved.leaderDeviceId)
    {
        std::set<std::string> liveFollowers;
        for (const auto &m : leaderStatus.members)
        {
            if (m.deviceId != leaderStatus.masterId)
            {
                liveFollowers.insert (m.deviceId);
            }
        }

        if (liveFollowers == expectedFollowers)
        {
            bool allSlavesAgree = true;
            for (size_t i = 1; i < resolved.memberDeviceIds.size (); ++i)
            {
                const SoundTouchClient::ZoneStatus &st = currentStatuses[i];
                if (!st.inZone || st.masterId != resolved.leaderDeviceId)
                {
                    allSlavesAgree = false;
                    break;
                }
            }

            bool allIpsMatch = true;
            for (size_t i = 1; i < resolved.memberDeviceIds.size (); ++i)
            {
                const std::string &devId = resolved.memberDeviceIds[i];
                const std::string &expIp = resolved.memberIps[i];
                bool foundMember = false;
                for (const auto &m : leaderStatus.members)
                {
                    if (m.deviceId == devId)
                    {
                        foundMember = true;
                        if (m.ipAddress != expIp)
                        {
                            allIpsMatch = false;
                        }
                        break;
                    }
                }
                if (!foundMember || !allIpsMatch)
                {
                    allIpsMatch = false;
                    break;
                }
            }

            if (allSlavesAgree && allIpsMatch)
            {
                // Exact topology already active; skip redundant setZone to preserve stream buffer
                return (true);
            }
        }
    }

    // Handle conflicts on leader
    if (leaderStatus.inZone)
    {
        if (!takeover)
        {
            error = "Leader speaker '" + resolved.leaderDeviceName + "' is in a live zone (leader: " + leaderStatus.masterId + "); takeover required";
            return (false);
        }

        if (leaderStatus.masterId == resolved.leaderDeviceId)
        {
            std::string disErr;
            if (!dissolve (resolved.leaderDeviceId, disErr, timeoutMs))
            {
                error = "Failed to clear existing followers on leader '" + resolved.leaderDeviceName + "': " + disErr;
                return (false);
            }
        }
        else
        {
            const SoundTouchDevice *oldMaster = m_config.findById (leaderStatus.masterId);
            if (oldMaster == nullptr)
            {
                error = "Cannot remove leader speaker '" + resolved.leaderDeviceName + "' from unknown leader '" + leaderStatus.masterId + "'";
                return (false);
            }

            SoundTouchClient masterClient (oldMaster->ipAddress);
            SoundTouchClient::ZoneMember follower;
            follower.deviceId = resolved.leaderDeviceId;
            follower.ipAddress = resolved.leaderIp;

            std::string remErr;
            if (!masterClient.removeZoneSlave (leaderStatus.masterId, follower, timeoutMs / 2, &remErr))
            {
                error = "Failed to remove leader speaker '" + resolved.leaderDeviceName + "' from prior leader '" + oldMaster->deviceName + "': " + remErr;
                return (false);
            }
        }
    }

    // Check and handle active playback on prospective followers
    for (size_t i = 1; i < resolved.memberDeviceIds.size (); ++i)
    {
        const std::string &memberId = resolved.memberDeviceIds[i];
        const SoundTouchClient::ZoneStatus &curStatus = currentStatuses[i];
        const SoundTouchDevice *dev = m_config.findById (memberId);
        const std::string name = (dev != nullptr ? dev->deviceName : memberId);

        bool alreadyInTargetZone = (curStatus.inZone && curStatus.masterId == resolved.leaderDeviceId);
        if (!alreadyInTargetZone)
        {
            SoundTouchClient client (resolved.memberIps[i]);
            SoundTouchClient::NowPlaying now = client.glance (timeoutMs / 2);
            if (now.source.empty () && now.status.empty ())
            {
                if (!takeover)
                {
                    error = "Could not query playback state for follower speaker '" + name + "'; takeover required";
                    return (false);
                }
            }
            else if (now.source != "STANDBY" && now.source != "INVALID_SOURCE" &&
                     (now.status == "PLAY_STATE" || now.status == "BUFFERING_STATE"))
            {
                if (!takeover)
                {
                    error = "Follower speaker '" + name + "' is actively playing audio (source: " + now.source + ", status: " + now.status + "); takeover required";
                    return (false);
                }
            }
        }
    }

    // Handle conflicts on followers
    for (size_t i = 1; i < resolved.memberDeviceIds.size (); ++i)
    {
        const std::string &memberId = resolved.memberDeviceIds[i];
        const SoundTouchClient::ZoneStatus &curStatus = currentStatuses[i];

        if (curStatus.inZone && curStatus.masterId != resolved.leaderDeviceId)
        {
            const SoundTouchDevice *dev = m_config.findById (memberId);
            const std::string name = (dev != nullptr ? dev->deviceName : memberId);

            if (!takeover)
            {
                error = "Member speaker '" + name + "' is part of another live zone (leader: " + curStatus.masterId + "); takeover required";
                return (false);
            }

            if (curStatus.masterId == memberId)
            {
                // Follower is itself the leader of another live zone; dissolve it
                std::string disErr;
                if (!dissolve (memberId, disErr, timeoutMs))
                {
                    error = "Failed to dissolve prior zone where member '" + name + "' was leader: " + disErr;
                    return (false);
                }
            }
            else
            {
                const SoundTouchDevice *oldMaster = m_config.findById (curStatus.masterId);

                if (oldMaster == nullptr)
                {
                    error = "Cannot remove member speaker '" + name + "' from unknown leader '" + curStatus.masterId + "'";
                    return (false);
                }

                SoundTouchClient masterClient (oldMaster->ipAddress);
                SoundTouchClient::ZoneMember follower;

                follower.deviceId = memberId;
                follower.ipAddress = resolved.memberIps[i];

                std::string remErr;

                if (!masterClient.removeZoneSlave (curStatus.masterId, follower, timeoutMs / 2, &remErr))
                {
                    error = "Failed to remove member speaker '" + name + "' from old leader '" + oldMaster->deviceName + "': " + remErr;
                    return (false);
                }
            }
        }
    }

    SoundTouchClient leaderClient (resolved.leaderIp);
    const std::string localRouteIp = leaderClient.getLocalRouteIp ();
    std::string setErr;

    if (!leaderClient.setZone (resolved.leaderDeviceId, localRouteIp, followers, timeoutMs, &setErr))
    {
        error = "Leader speaker rejected setZone: " + setErr;
        return (false);
    }

    bool allVerified = false;
    std::string unverifiedMember;
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (std::min (timeoutMs, 5000L));

    while (std::chrono::steady_clock::now () < deadline)
    {
        SoundTouchClient::ZoneStatus checkLeaderStatus;

        if (leaderClient.getZone (checkLeaderStatus, 1000) && checkLeaderStatus.inZone && checkLeaderStatus.masterId == resolved.leaderDeviceId)
        {
            allVerified = true;

            for (const auto &follower : followers)
            {
                SoundTouchClient followerClient (follower.ipAddress);
                SoundTouchClient::ZoneStatus followerStatus;

                if (!followerClient.getZone (followerStatus, 1000) || !followerStatus.inZone || followerStatus.masterId != resolved.leaderDeviceId)
                {
                    allVerified = false;
                    unverifiedMember = follower.deviceId;
                    break;
                }
            }

            if (allVerified)
            {
                break;
            }
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (250));
    }

    if (!allVerified)
    {
        std::string rollbackErr;
        const bool rbSuccess = dissolve (resolved.leaderDeviceId, rollbackErr, timeoutMs / 2);

        const SoundTouchDevice *unverifiedDev = m_config.findById (unverifiedMember);
        const std::string name = (unverifiedDev != nullptr ? unverifiedDev->deviceName : unverifiedMember);

        error = "Zone activation read-back verification failed: member '" + name + "' did not join zone;";
        if (rbSuccess)
        {
            error += " safely rolled back";
        }
        else
        {
            error += " rollback also failed: " + rollbackErr;
        }

        return (false);
    }

    return (true);
}

bool ZoneManager::dissolve (const std::string &zoneOrLeaderId, std::string &error, long timeoutMs)
{
    std::string leaderDeviceId = zoneOrLeaderId;
    const ZoneConfig *zone = m_config.findZoneById (zoneOrLeaderId);

    if (zone != nullptr)
    {
        leaderDeviceId = zone->leaderId;
    }

    const SoundTouchDevice *leaderDev = m_config.findById (leaderDeviceId);

    if (leaderDev == nullptr)
    {
        error = "Leader speaker '" + leaderDeviceId + "' not found in configuration";
        return (false);
    }

    SoundTouchClient leaderClient (leaderDev->ipAddress);
    SoundTouchClient::ZoneStatus status;
    std::string getErr;

    if (!leaderClient.getZone (status, timeoutMs / 2, &getErr))
    {
        error = "Failed to query zone status from leader '" + leaderDev->deviceName + "': " + getErr;
        return (false);
    }

    if (!status.inZone)
    {
        return (true);
    }

    bool anyRemoveFailed = false;
    std::string removeErr;

    for (const auto &member : status.members)
    {
        if (member.deviceId == status.masterId)
        {
            continue;
        }

        std::string devErr;

        if (!leaderClient.removeZoneSlave (status.masterId, member, timeoutMs / 2, &devErr))
        {
            anyRemoveFailed = true;
            removeErr = devErr;
        }
    }

    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (std::min (timeoutMs, 4000L));
    bool dissolved = false;

    while (std::chrono::steady_clock::now () < deadline)
    {
        SoundTouchClient::ZoneStatus checkStatus;

        if (leaderClient.getZone (checkStatus, 1000) && !checkStatus.inZone)
        {
            dissolved = true;
            break;
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (250));
    }

    if (!dissolved)
    {
        error = "Leader failed to dissolve zone within timeout" + (anyRemoveFailed ? (": " + removeErr) : "");
        return (false);
    }

    return (true);
}

ZoneManager::ZoneSnapshot ZoneManager::snapshot (long timeoutMs)
{
    ZoneSnapshot snap;

    for (const SoundTouchDevice &dev : m_config.getSpeakers ())
    {
        DeviceLiveState state;
        state.deviceId = dev.deviceId;
        state.ipAddress = dev.ipAddress;
        state.deviceName = dev.deviceName;

        SoundTouchClient client (dev.ipAddress);
        SoundTouchClient::ZoneStatus zStatus;
        std::string err;

        if (client.getZone (zStatus, timeoutMs, &err))
        {
            state.online = true;
            state.inZone = zStatus.inZone;
            state.isLeader = zStatus.isLeader;
            state.liveMasterId = zStatus.masterId;

            for (const auto &m : zStatus.members)
            {
                if (m.deviceId != zStatus.masterId)
                {
                    state.liveFollowerIds.push_back (m.deviceId);
                }
            }
        }
        else
        {
            state.online = false;
            state.error = err;
        }

        snap.devices.push_back (state);
    }

    return (snap);
}
