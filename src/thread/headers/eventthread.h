/*
** eventthread.h
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef EVENTTHREAD_H
#define EVENTTHREAD_H

#include "config.h"
#include "etc-internal.h"
#include "sdl-util.h"
#include "keybindings.h"

#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_mutex.h>

#include <string>

#include <stdint.h>

#include <alc.h>

struct RGSSThreadData;
struct SDL_Window;
union SDL_Event;

#define MAX_FINGERS 4

class EventThread
{
public:
	struct ControllerState
	{
		int axes[SDL_CONTROLLER_AXIS_MAX];
		bool buttons[SDL_CONTROLLER_BUTTON_MAX];
	};

	struct JoyState
	{
		int axes[256];
		uint8_t hats[256];
		bool buttons[256];
	};

	struct MouseState
	{
		int x, y;
		bool inWindow;
		bool buttons[32];
	};

	struct FingerState
	{
		bool down;
		int x, y;
	};

	struct TouchState
	{
		FingerState fingers[MAX_FINGERS];
	};

	static uint8_t keyStates[SDL_NUM_SCANCODES + BUTTONCODE_SDLK_COUNT];
	static Uint16 modkeys;
	static ControllerState gcState;
	static JoyState joyState;
	static MouseState mouseState;
	static TouchState touchState;
	static bool forceTerminate;

	static bool allocUserEvents();

	EventThread();

	void process(RGSSThreadData &rtData);
	void cleanup();

	/* Called from RGSS thread */
	void requestFullscreenMode(bool mode);
	void requestWindowResize(int width, int height);
	void requestShowCursor(bool mode);

	void requestTerminate();

	bool getFullscreen() const;
	bool getShowCursor() const;

	void showMessageBox(const char *body, int flags = 0);

	/* RGSS thread calls this once per frame */
	void notifyFrame();

	/* Called on game screen (size / offset) changes */
	void notifyGameScreenChange(const SDL_Rect &screen);

	static SDL_mutex *inputMut;

private:
	static int eventFilter(void *, SDL_Event*);

	void resetInputStates();
	void setFullscreen(SDL_Window *, bool mode);
	void updateCursorState(bool inWindow,
	                       const SDL_Rect &screen);

	bool fullscreen;
	bool showCursor;
	AtomicFlag msgBoxDone;

	struct
	{
		uint64_t lastFrame;
		uint64_t displayCounter;
		AtomicFlag sendUpdates;
		AtomicFlag immInitFlag;
		AtomicFlag immFiniFlag;
		double acc;
		uint32_t accDiv;
	} fps;
};

/* Used to asynchronously inform the RGSS thread
 * about certain value changes */
template<typename T>
struct UnidirMessage
{
	UnidirMessage()
	    : mutex(SDL_CreateMutex()),
	      current(T())
	{}

	~UnidirMessage()
	{
		SDL_DestroyMutex(mutex);
	}

	/* Done from the sending side */
	void post(const T &value)
	{
		SDL_LockMutex(mutex);

		changed.set();
		current = value;

		SDL_UnlockMutex(mutex);
	}

	/* Done from the receiving side */
	bool poll(T &out) const
	{
		if (!changed)
			return false;

		SDL_LockMutex(mutex);

		out = current;
		changed.clear();

		SDL_UnlockMutex(mutex);

		return true;
	}

	/* Done from either */
	void get(T &out) const
	{
		SDL_LockMutex(mutex);
		out = current;
		SDL_UnlockMutex(mutex);
	}

private:
	SDL_mutex *mutex;
	mutable AtomicFlag changed;
	T current;
};

struct SyncPoint
{
	/* Used by eventFilter to control sleep/wakeup */
	void haltThreads();
	void resumeThreads();

	/* Used by RGSS thread */
	bool mainSyncLocked();
	void waitMainSync();

	/* Used by secondary (audio) threads */
	void passSecondarySync();

private:
	struct Util
	{
		Util();
		~Util();

		void lock();
		void unlock(bool multi);
		void waitForUnlock();

		AtomicFlag locked;
		SDL_mutex *mut;
		SDL_cond *cond;
	};

	Util mainSync;
	Util reply;
	Util secondSync;
};

struct RGSSThreadData
{
	/* Main thread sets this to request RGSS thread to terminate */
	AtomicFlag rqTerm;
	/* In response, RGSS thread sets this to confirm
	 * that it received the request and isn't stuck */
	AtomicFlag rqTermAck;

	/* Set when F12 is pressed */
	AtomicFlag rqReset;

	/* Set when F12 is released */
	AtomicFlag rqResetFinish;

	/* True if we're currently exiting */
	AtomicFlag exiting;

	/* True if exiting is allowed */
	AtomicFlag allowExit;

	/* Set when attempting to exit and allowExit is false */
	AtomicFlag triedExit;

	/* True if accepting text input */
	AtomicFlag acceptingTextInput;

	/* True if allow force quit */
	AtomicFlag allowForceQuit;

	EventThread *ethread;
	UnidirMessage<Vec2i> windowSizeMsg;
	UnidirMessage<BDescVec> bindingUpdateMsg;
	SyncPoint syncPoint;

	SDL_Window *window;
	ALCdevice *alcDev;

	Vec2 sizeResoRatio;
	Vec2i screenOffset;
	const int refreshRate;

	Config config;

	std::string rgssErrorMsg;
	std::string inputText;
	int inputTextLimit;

	RGSSThreadData(EventThread *ethread,
	               SDL_Window *window,
	               ALCdevice *alcDev,
	               int refreshRate,
	               const Config& newconf)
	    : allowExit(true),
	      ethread(ethread),
	      window(window),
	      alcDev(alcDev),
	      sizeResoRatio(1, 1),
	      refreshRate(refreshRate),
	      config(newconf)
	{}
};

#endif // EVENTTHREAD_H
