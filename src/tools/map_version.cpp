/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>
#include <base/tl/sorted_array.h>

#include <engine/kernel.h>
#include <engine/map.h>
#include <engine/shared/jsonparser.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/version.h>

struct CMapVersion
{
	char m_aName[8];
	int64 m_Size;
	int64 m_Crc;
	char m_aSha256[SHA256_DIGEST_LENGTH];
	char m_aVersion[32];

	bool operator<(const CMapVersion &Other) const
	{
		int VersionComp = str_comp(m_aVersion, Other.m_aVersion);
		if(VersionComp != 0)
			return VersionComp < 0;
		return str_comp(m_aName, Other.m_aName) < 0;
	}
	bool operator==(const CMapVersion &Other) const { return (str_comp(m_aName, Other.m_aName) == 0) &&
								 (str_comp(m_aSha256, Other.m_aSha256) == 0) &&
								 m_Size == Other.m_Size &&
								 m_Crc == Other.m_Crc; }
};

static IOHANDLE s_File = 0;
static IStorage *s_pStorage = 0;
static IEngineMap *s_pEngineMap = 0;
static CJsonWriter *s_pJsonWriter = 0;
static sorted_array<CMapVersion> s_lMapVersions;

int MaplistCallback(const char *pName, int IsDir, int DirType, void *pUser)
{
	int l = str_length(pName);
	if(IsDir || str_endswith_nocase(pName, ".map") == 0)
		return 0;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s", pName);
	if(!s_pEngineMap->Load(aBuf))
		return 0;

	CMapVersion MapVersion;
	MapVersion.m_Crc = s_pEngineMap->Crc();
	sha256_str(s_pEngineMap->Sha256(), MapVersion.m_aSha256, sizeof(MapVersion.m_aSha256));
	s_pEngineMap->Unload();

	IOHANDLE MapFile = s_pStorage->OpenFile(aBuf, IOFLAG_READ, DirType);
	MapVersion.m_Size = io_length(MapFile);
	io_close(MapFile);

	str_copy(MapVersion.m_aName, pName, minimum((int) sizeof(MapVersion.m_aName), l - 3));
	str_copy(MapVersion.m_aVersion, GAME_VERSION, sizeof(MapVersion.m_aVersion));

	// check duplicate
	for(sorted_array<CMapVersion>::range r = s_lMapVersions.all(); !r.empty(); r.pop_front())
	{
		if(MapVersion == r.front())
			return 0;
	}
	s_lMapVersions.add(MapVersion);

	return 0;
}

int main(int argc, const char **argv)
{
	cmdline_fix(&argc, &argv);

	IKernel *pKernel = IKernel::Create();
	s_pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_BASIC, argc, argv);
	s_pEngineMap = CreateEngineMap();

	bool RegisterFail = !pKernel->RegisterInterface(s_pStorage);
	RegisterFail |= !pKernel->RegisterInterface(s_pEngineMap);

	if(RegisterFail)
		return -1;

	{
		CJsonParser Parser;
		const json_value *pJsonData = Parser.ParseFile("map_version.json", s_pStorage, IStorage::TYPE_APP);
		if(pJsonData)
		{
			// extract data
			const json_value &rStart = (*pJsonData)["maps"];
			if(rStart.type == json_array)
			{
				for(unsigned i = 0; i < rStart.u.array.length; ++i)
				{
					CMapVersion Version;
					str_copy(Version.m_aName, rStart[i]["name"].u.string.ptr, sizeof(Version.m_aName));
					Version.m_Size = rStart[i]["size"].u.integer;
					Version.m_Crc = rStart[i]["crc"].u.integer;
					str_copy(Version.m_aSha256, rStart[i]["sha256"].u.string.ptr, sizeof(Version.m_aSha256));
					str_copy(Version.m_aVersion, rStart[i]["version"].u.string.ptr, sizeof(Version.m_aVersion));
					s_lMapVersions.add(Version);
				}
			}
		}
	}

	s_File = s_pStorage->OpenFile("map_version.json", IOFLAG_WRITE, IStorage::TYPE_APP);
	if(s_File)
	{
		file_stream Stream(s_File, true);
		s_pJsonWriter = new CJsonWriter(&Stream);

		s_pStorage->ListDirectory(1, "maps", MaplistCallback, 0x0);

		s_pJsonWriter->BeginObject();
		s_pJsonWriter->WriteAttribute("maps");
		s_pJsonWriter->BeginArray();

		for(sorted_array<CMapVersion>::range r = s_lMapVersions.all(); !r.empty(); r.pop_front())
		{
			s_pJsonWriter->BeginObject();

			s_pJsonWriter->WriteAttribute("name");
			s_pJsonWriter->WriteStrValue(r.front().m_aName);

			s_pJsonWriter->WriteAttribute("size");
			s_pJsonWriter->WriteInt64Value(r.front().m_Size);

			s_pJsonWriter->WriteAttribute("crc");
			s_pJsonWriter->WriteInt64Value(r.front().m_Crc);

			s_pJsonWriter->WriteAttribute("sha256");
			s_pJsonWriter->WriteStrValue(r.front().m_aSha256);

			s_pJsonWriter->WriteAttribute("version");
			s_pJsonWriter->WriteStrValue(r.front().m_aVersion);

			s_pJsonWriter->EndObject();
		}
		s_pJsonWriter->EndArray();
		s_pJsonWriter->EndObject();

		delete s_pJsonWriter;
	}

	cmdline_free(argc, argv);
	return 0;
}
