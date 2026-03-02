#include "Common/VR/VRCompatRating.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Common/Data/Format/IniFile.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "Core/System.h"

static std::map<std::string, VRCompat> s_vrCompatCache;
static std::set<std::string> s_knownGameIDs;
static std::once_flag s_initFlag;

static void LoadVRCompatFromIni(IniFile &iniFile) {
	const char *sectionNames[] = {
		"ForceFlatScreen", "ForceMono", "IdentityViewHack",
		"MirroringVariant", "ProjectionHack", "Skyplane", "UnitsPerMeter"
	};

	// Collect all known game IDs from all sections
	for (const char *secName : sectionNames) {
		Section *sec = iniFile.GetSection(secName);
		if (!sec) continue;
		std::vector<std::string> keys;
		sec->GetKeys(&keys);
		for (auto &k : keys) {
			if (k != "ALL") {
				s_knownGameIDs.insert(k);
			}
		}
	}

	// Build VRCompat for each known game ID
	for (const auto &id : s_knownGameIDs) {
		// If this game already has a cache entry (from a previous INI load),
		// start from that; otherwise start from default-constructed.
		VRCompat compat{};
		auto it = s_vrCompatCache.find(id);
		if (it != s_vrCompatCache.end()) {
			compat = it->second;
		}

		Section *sec;

		if ((sec = iniFile.GetSection("ForceFlatScreen")) != nullptr) {
			sec->Get(id, &compat.ForceFlatScreen);
		}
		if ((sec = iniFile.GetSection("ForceMono")) != nullptr) {
			sec->Get(id, &compat.ForceMono);
		}
		if ((sec = iniFile.GetSection("IdentityViewHack")) != nullptr) {
			sec->Get(id, &compat.IdentityViewHack);
		}
		if ((sec = iniFile.GetSection("ProjectionHack")) != nullptr) {
			sec->Get(id, &compat.ProjectionHack);
		}
		if ((sec = iniFile.GetSection("Skyplane")) != nullptr) {
			sec->Get(id, &compat.Skyplane);
		}
		if ((sec = iniFile.GetSection("UnitsPerMeter")) != nullptr) {
			std::string val;
			if (sec->Get(id, &val) && !val.empty()) {
				compat.UnitsPerMeter = stof(val);
			}
		}
		if ((sec = iniFile.GetSection("MirroringVariant")) != nullptr) {
			std::string val;
			if (sec->Get(id, &val) && !val.empty()) {
				compat.MirroringVariant = stoi(val);
			}
		}

		s_vrCompatCache[id] = compat;
	}
}

void InitVRCompatCache() {
	std::call_once(s_initFlag, []() {
		// Load from bundled assets first
		IniFile compat;
		if (compat.LoadFromVFS(g_VFS, "compatvr.ini")) {
			LoadVRCompatFromIni(compat);
		}

		// Load user-editable file on top (same pattern as Compatibility::Load).
		// GetSysDirectory may not be available if system dirs aren't initialized yet;
		// in that case the asset load above is sufficient for the game browser.
		Path sysPath = GetSysDirectory(DIRECTORY_SYSTEM);
		if (!sysPath.empty()) {
			IniFile userCompat;
			Path path = sysPath / "compatvr.ini";
			if (userCompat.Load(path)) {
				LoadVRCompatFromIni(userCompat);
			}
		}

		NOTICE_LOG(Log::G3D, "VRCompatCache: loaded %zu known games", s_knownGameIDs.size());
	});
}

VRCompatInfo GetVRCompatInfo(const std::string &gameID) {
	InitVRCompatCache();

	VRCompatInfo info{};
	info.known = s_knownGameIDs.count(gameID) > 0;

	auto it = s_vrCompatCache.find(gameID);
	if (it != s_vrCompatCache.end()) {
		info.compat = it->second;
	}

	return info;
}

int DeriveVRStarRating(const VRCompat &compat, bool isKnown) {
	if (!isKnown) return 0;  // question mark

	if (compat.ForceFlatScreen) return 2;  // forced cinema

	if (compat.ForceMono && compat.UnitsPerMeter == 0.0f) return 1;  // known issues

	if (compat.ForceMono) return 3;  // mono immersive (has UnitsPerMeter)

	if (compat.UnitsPerMeter > 0.0f && !compat.IdentityViewHack) return 5;  // full stereo+6DoF

	if (compat.UnitsPerMeter > 0.0f) return 4;  // stereo, limited 6DoF

	return 3;  // in database but minimal flags set
}
