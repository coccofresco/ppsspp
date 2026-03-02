#pragma once

#include <string>
#include "Core/Compatibility.h"  // for VRCompat

struct VRCompatInfo {
	VRCompat compat;  // The 7 flags from compatvr.ini
	bool known;       // true if gameID appears in any compatvr.ini section
};

// Load compatvr.ini into static cache. Safe to call multiple times (lazy init with std::call_once).
// Must be called after g_VFS is initialized (which happens early in NativeInit).
void InitVRCompatCache();

// Thread-safe lookup. Returns VRCompatInfo with known=false for unknown games.
// Does NOT require a running game (uses cached data, not PSP_CoreParameter).
VRCompatInfo GetVRCompatInfo(const std::string &gameID);

// Pure function: derive 1-5 star rating from VRCompat flags.
// Returns 0 for unknown games (isKnown=false) -> UI shows question mark.
// Rating logic:
//   5 = stereo+6DoF capable (UnitsPerMeter set, no IdentityViewHack, no ForceMono)
//   4 = stereo without 6DoF (UnitsPerMeter set + IdentityViewHack)
//   3 = mono immersive (ForceMono with UnitsPerMeter, or in DB but no UnitsPerMeter)
//   2 = forced cinema (ForceFlatScreen)
//   1 = known issues (ForceMono + no UnitsPerMeter)
int DeriveVRStarRating(const VRCompat &compat, bool isKnown);
