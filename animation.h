#pragma once
// -----------------------------------------------------------------------------
// animation.h — grid-based sprite sheet timing and UV offsets
// -----------------------------------------------------------------------------
// Frames are laid out in rows on a texture; getSpriteUVOffset* returns the
// top-left UV for the current frame so the fragment shader can sample a cell.

#include "Timer.h"

class Animation {
	Timer timer = Timer(0);
	int frameCount = 1;
	int currentFrame = 0;
	float spriteUVOffsetX = 0.0f;
	float spriteUVOffsetY = 0.0f;
	int framesPerRow = 1;

public:
	Animation() = default;
	Animation(float length, int frameCount, int spriteWidth, int spriteHeight, int spriteSheetWidth, int spriteSheetHeight, int currentFrame=0) :
		timer(length),
		frameCount(frameCount),
		currentFrame(currentFrame),
		spriteUVOffsetX(spriteWidth / (float) spriteSheetWidth),
		spriteUVOffsetY(spriteHeight / (float) spriteSheetHeight),
		framesPerRow(spriteSheetWidth / spriteWidth)
	{
	}
	float getLength() const { return timer.getLength(); }

	int getCurrentFrame() const
	{
		return currentFrame;
	}

	float getSpriteUVOffsetX() const
	{
		return static_cast<int>(getCurrentFrame() % framesPerRow) * spriteUVOffsetX;
	}
	float getSpriteUVOffsetY() const
	{
		return -static_cast<int>(getCurrentFrame() / framesPerRow) * spriteUVOffsetY;
	}


	void step(float deltaTime)
	{
		timer.step(deltaTime);
		if (timer.isTimedOut() && frameCount > currentFrame + 1)
		{
			currentFrame = (currentFrame + 1) % frameCount;
			timer.restart();
		}
	}
	float getTime() const
	{
		return timer.getTime();
	}
};
