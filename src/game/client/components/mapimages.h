/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_MAPIMAGES_H
#define GAME_CLIENT_COMPONENTS_MAPIMAGES_H
#include <engine/storage.h>

#include <game/client/component.h>

class CMapImages : public CComponent
{
public:
	enum
	{
		MAX_TEXTURES = 64,

		MAP_TYPE_GAME = 0,
		MAP_TYPE_MENU, // first menu map slot (cross-fade)
		MAP_TYPE_MENU_B, // second menu map slot (cross-fade)
		MAP_TYPE_GAME_FADE, // the map of the world we just left, fading out
		NUM_MAP_TYPES
	};

private:
	struct
	{
		struct
		{
			IGraphics::CTextureHandle m_Quads;
			IGraphics::CTextureHandle m_Tilemap;
		} m_aTextures[MAX_TEXTURES];
		int m_Count;
	} m_Info[NUM_MAP_TYPES];

	// decoded tilesets shared by path across maps
	struct SCachedImage
	{
		char m_aPath[IO_MAX_PATH_LENGTH];
		IGraphics::CTextureHandle m_Quads;
		IGraphics::CTextureHandle m_Tilemap;
		int64 m_PixelBytes;
		int64 m_LastUse;
	};
	enum
	{
		// decoded tilesets are big, keep the cache bounded
		IMAGE_CACHE_BUDGET = 64 * 1024 * 1024,
	};
	array<SCachedImage> m_lImageCache;
	int64 m_CacheBytes;
	int64 m_CacheClock;

	void LoadMapImages(class CLayers *pLayers, int MapType);
	IGraphics::CTextureHandle CacheImage(const char *pPath, bool IsQuads, int Width, int Height);
	bool IsCachedImage(const IGraphics::CTextureHandle &Handle) const;
	bool IsCacheEntryUsed(const SCachedImage &Entry) const;
	bool EvictOneImage();

	IGraphics::CTextureHandle m_EasterTexture;
	bool m_EasterIsLoaded;

public:
	CMapImages();

	IGraphics::CTextureHandle Get(int Index, bool IsQuads, int MapType = -1) const;
	int Num() const;

	virtual void OnMapLoad() override;
	virtual void OnMapUnload() override;
	void OnMenuMapLoad(class IMap *pMap, int MapIndex, int MapType = MAP_TYPE_MENU);
	void UnloadMap(int MapType);
	// move textures to another map type without unloading
	void MoveMapType(int From, int To);

	IGraphics::CTextureHandle GetEasterTexture();
};

#endif
