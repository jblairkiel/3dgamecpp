#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <chrono>

#include "std_types.hpp"

class ServerInterface;
class World;
class Menu;
class Graphics;
class GraphicsConf;
class Stopwatch;

namespace gui {
	class Frame;
}

enum ClockId {
	CLOCK_CLR,
	CLOCK_NDL,
	CLOCK_DLC,
	CLOCK_CHL,
	CLOCK_CHR,
	CLOCK_PLA,
	CLOCK_HUD,
	CLOCK_FLP,
	CLOCK_TIC,
	CLOCK_NET,
	CLOCK_SYN,
	CLOCK_FSH,
	CLOCK_ALL,

	CLOCK_ID_NUM
};

class Client {
private:
	ServerInterface *serverInterface = nullptr;
	World *world = nullptr;
	Menu *menu = nullptr;
	gui::Frame *frame = nullptr;
	Graphics *graphics = nullptr;
	GraphicsConf *conf = nullptr;
	Stopwatch *stopwatch = nullptr;

	int localClientID;

	time_t time = 0;
	time_t timeShift = 0;

	bool closeRequested = false;

public:
	Client(const Client &) = delete;
	Client();
	~Client();

	void run();

private:
	void sync(int perSecond);

	void handleInput();
};

#endif // CLIENT_HPP