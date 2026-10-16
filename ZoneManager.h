#ifndef ZONE_MANAGER_H
#define ZONE_MANAGER_H

#include "SpeakerConfig.h"
#include "SoundTouchClient.h"
#include <string>
#include <vector>

// Synchronous management of live SoundTouch multiroom zones and target activation.
class ZoneManager
{
    public :

        // Live state of a single speaker on the network
        struct DeviceLiveState
        {
            std::string              deviceId;
            std::string              ipAddress;
            std::string              deviceName;
            bool                     online = false;
            bool                     inZone = false;
            bool                     isLeader = false;
            std::string              liveMasterId;
            std::vector<std::string> liveFollowerIds;
            std::string              error;
        };

        // Preflight topology report for activating a target
        struct PreflightReport
        {
            bool                         ready = false;
            bool                         conflict = false;
            std::string                  message;
            std::vector<DeviceLiveState> devices;
        };

        // Snapshot of live zone topology across all known speakers
        struct ZoneSnapshot
        {
            std::vector<DeviceLiveState> devices;
        };

        explicit ZoneManager (const SpeakerConfig &config);

        // Preflight check with NO side effects:
        // Verifies online reachability and live group membership for all required speakers.
        // Checks exact actual membership: flags conflict if members are in another group or
        // if leader currently has extra/different followers that would be disturbed.
        PreflightReport preflight (const PlaybackTarget &target, bool takeover = false, long timeoutMs = 5000);

        // Followers report only themselves in /getZone; only the leader's view holds full membership.
        static bool liveMembershipMatches (const std::vector<DeviceLiveState> &devices,
                                           const std::vector<std::string> &expectedMemberIds,
                                           const std::string &leaderDeviceId);

        // Activates target (solo speaker or zone):
        // 1. Validates definition and resolves target to leader and members.
        // 2. Checks all members online (returns false if any is offline).
        // 3. Checks if exact topology already active: skips redundant setZone if so.
        // 4. Handles conflicts (fails if conflict and !takeover; if takeover, clears old memberships).
        // 5. Sets zone on leader (or dissolves if solo).
        // 6. Read-back verifies all members joined.
        // 7. On partial verification failure: attempts safe rollback and returns false with error.
        bool activate (const PlaybackTarget &target, bool takeover, std::string &error, long timeoutMs = 8000);

        // Dissolves live zone for given zone ID or leader device ID:
        // Removes followers from leader and read-back verifies dissolution.
        bool dissolve (const std::string &zoneOrLeaderId, std::string &error, long timeoutMs = 8000);

        // Takes a current live snapshot across all configured speakers.
        ZoneSnapshot snapshot (long timeoutMs = 5000);

    private :

        // Now the data members

        const SpeakerConfig &m_config;
};

#endif
