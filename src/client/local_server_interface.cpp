#include "local_server_interface.hpp"

#include <thread>
#include <atomic>

#include "shared/engine/logging.hpp"
#include "shared/saves.hpp"
#include "client/gfx/graphics.hpp"

using namespace std;

static logging::Logger logger("local");

LocalServerInterface::LocalServerInterface(Client *client) :
	client(client),
	worldGenerator(client->getSave()->getWorldGenerator()),
	loadedQueue(1024),
	toLoadQueue(1024)
{
	client->getWorld()->addPlayer(0);
	player = &client->getWorld()->getPlayer(0);
}

LocalServerInterface::~LocalServerInterface() {
	client->getWorld()->deletePlayer(0);
}

ServerInterface::Status LocalServerInterface::getStatus() {
	return CONNECTED;
}

int LocalServerInterface::getLocalClientId() {
	return 0;
}

void LocalServerInterface::setConf(const GraphicsConf &conf, const GraphicsConf &old) {
	if (conf.fog == old.fog)
		return;
	else
		return;
}

void LocalServerInterface::tick() {
	client->getWorld()->tick(0);
}

void LocalServerInterface::doWork() {
	Chunk *chunk;
	if (toLoadQueue.pop(chunk)) {
		worldGenerator->generateChunk(chunk);
		while (!loadedQueue.push(chunk)) {
			sleepFor(millis(50));
		}
	} else {
		sleepFor(millis(100));
	}
}

void LocalServerInterface::setPlayerMoveInput(int moveInput) {
	if (player->isValid())
		player->setMoveInput(moveInput);
}

void LocalServerInterface::setPlayerOrientation(int yaw, int pitch) {
	player->setOrientation(yaw, pitch);
}

void LocalServerInterface::setSelectedBlock(uint8 block) {
	player->setBlock(block);
}

void LocalServerInterface::placeBlock(vec3i64 blockCoords, uint8 blockType) {
	vec3i64 cc = bc2cc(blockCoords);
	vec3ui8 icc = bc2icc(blockCoords);
	const Chunk *chunk = client->getChunkManager()->getChunk(cc);
	if (chunk) {
		size_t blockIndex = Chunk::getBlockIndex(icc);
		client->getChunkManager()->placeBlock(
			cc,
			blockIndex,
			blockType,
			chunk->getRevision()
		);
	}
	// TODO tell world
	// TODO maybe move this to graphics or something
	client->getRenderer()->rebuildChunk(cc);
	for (size_t i = 0; i < 27; i++) {
		if (i == BIG_CUBE_CYCLE_BASE_INDEX)
			continue;
		bool rerender = true;
		for (int d = 0; d < 3 && rerender == true; d++) {
			switch (BIG_CUBE_CYCLE[i][d]) {
				case 0:
					break;
				case -1:
					if (icc[d] != 0)
						rerender = false;
					break;
				case 1:
					if (icc[d] != Chunk::WIDTH - 1)
						rerender = false;
					break;
				default:
					rerender = false;
					break;
			}
		}
		if (rerender)
			client->getRenderer()->rebuildChunk(cc + BIG_CUBE_CYCLE[i].cast<int64>());
	}
}

void LocalServerInterface::toggleFly() {
	player->setFly(!player->getFly());
}

bool LocalServerInterface::requestChunk(Chunk *chunk) {
	return toLoadQueue.push(chunk);
}

Chunk *LocalServerInterface::getNextChunk() {
	Chunk *chunk = nullptr;
	loadedQueue.pop(chunk);
	return chunk;
}
