#pragma once
// -----------------------------------------------------------------------------
// Timer.h — small repeating interval flag (used for FPS counting and animation)
// -----------------------------------------------------------------------------
// step() accumulates time; when it crosses `length`, timedOut becomes true until
// reset()/restart(). Animation uses restart() to advance frames.

class Timer {
	float time, length;
	bool timedOut, startTimedOut;
public:
	Timer() : time(0.0f), length(0.2f), timedOut(false), startTimedOut(false)
	{
	}
	Timer(float length, bool timedOutStartingState = false) : time(0), length(length), startTimedOut(timedOutStartingState)
	{
		timedOut = timedOutStartingState;
	}
	void step(float deltaTime) {

		time += deltaTime;
		if (time >= length) {
			time -= length;
			timedOut = true;
		}
	}
	bool isTimedOut() const { return timedOut; }
	void fullReset() { time = 0; timedOut = startTimedOut ? true : false; }
	void reset() { time = 0; timedOut = false; }
	void restart() { timedOut = false; }
	float getTime() const { return time; }
	float getLength() const { return length; }
	void updateLengthMeasure(float t) { length = t; }
	void timeOutTimer() { timedOut = true; }
};
