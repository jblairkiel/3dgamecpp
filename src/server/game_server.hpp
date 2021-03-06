#ifndef GAME_SERVER_HPP
#define GAME_SERVER_HPP

#include "server.hpp"

#include "shared/game/world.hpp"

struct Player {
	bool valid = false;
	std::string name;
};

class GameServer {
private:
	Server *server;
	World *world;

	Player players[MAX_CLIENTS];

public:
	GameServer(Server *server);
	~GameServer();

	void tick();

	void onPlayerJoin(int id, PlayerInfo &info);
	void onPlayerLeave(int id, DisconnectReason reason);
	void onPlayerInput(int id, PlayerInput &input);

private:
	void sendSnapshots(int tick);
};

#endif // GAME_SERVER_HPP
