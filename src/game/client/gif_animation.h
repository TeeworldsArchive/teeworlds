/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_GIF_ANIMATION_H
#define GAME_CLIENT_GIF_ANIMATION_H

#include <base/tl/array.h>

#include <engine/graphics.h>

/*
	Plays a GIF as a rolling window of textures.

	Steam's animated avatars can have hundreds of frames, and keeping all of
	them as textures would cost tens of megabytes per friend. The compressed
	file is kept instead, the frames are decoded as they are played and only a
	small window of textures around the current frame is resident, so long
	animations are streamed like a video and repeating them is cheap.

	Only available when the client was built with giflib (CONF_GIFLIB).
*/
class CGifAnimation
{
public:
	CGifAnimation();
	~CGifAnimation();

	// owns textures and a decoder, copying one would double free both
	CGifAnimation(const CGifAnimation &) = delete;
	CGifAnimation &operator=(const CGifAnimation &) = delete;

	void Reset(IGraphics *pGraphics);
	bool LoadRGBA(IGraphics *pGraphics, int Width, int Height, const unsigned char *pData);
	bool LoadGIF(IGraphics *pGraphics, const unsigned char *pData, int DataSize);

	// Advances the playback; call once per frame while the image is visible.
	// New frames are decoded and uploaded from here.
	void Update(IGraphics *pGraphics, float Seconds);
	IGraphics::CTextureHandle Texture() const;

	int Width() const { return m_Width; }
	int Height() const { return m_Height; }
	bool IsValid() const { return m_lTextures.size() > 0; }
	bool IsAnimated() const { return m_bAnimated; }

private:
	class CGifStream *m_pStream;

	// ring of textures around the frame being played
	array<IGraphics::CTextureHandle> m_lTextures;
	array<int> m_lDelaysMs;
	// absolute frame each slot holds, -1 while unused; the frame number keeps
	// counting across loops so a slot can tell an old frame from a new one
	array<int> m_lFrameIndices;

	int m_Width;
	int m_Height;
	int m_CurrentFrame;
	int m_DecodedFrame;
	bool m_bAnimated;
	float m_FrameTime;

	bool DecodeNextFrame(IGraphics *pGraphics);
	bool RewindStream();
	void Prefetch(IGraphics *pGraphics);
	void UploadTexture(IGraphics *pGraphics, int Frame, int DelayMs, const unsigned char *pData);
};

#endif