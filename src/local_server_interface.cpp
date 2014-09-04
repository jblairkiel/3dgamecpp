#include "local_server_interface.hpp"

LocalServerInterface::LocalServerInterface(World *world, uint64 seed)
		: chunkLoader(world, seed, true) {
	this->world = world;
	world->addPlayer(0);
	chunkLoader.dispatch();
}


LocalServerInterface::~LocalServerInterface() {
	// nothing
}

void LocalServerInterface::togglePlayerFly() {
	world->getPlayer(0).setFly(!world->getPlayer(0).getFly());
}

void LocalServerInterface::setPlayerMoveInput(int moveInput) {
	//		world.getPlsayer(0).setMoveInput(moveInput);
}

void LocalServerInterface::setPlayerOrientation(double yaw, double pitch) {
	//		world.getPlayer(0).setOrientation(yaw, pitch);
}

void LocalServerInterface::edit(vec3i64 bc, uint8 type) {
	world->setBlock(bc, type, true);
}

void LocalServerInterface::receive(uint64 timeLimit) {
	Chunk *chunk = nullptr;
	while ((chunk = chunkLoader.next()) != nullptr) {
		chunk->patchBorders(world);
		world->getChunks().insert({chunk->cc, chunk});
	}

	auto unloadQueries = chunkLoader.getUnloadQueries();
	while (unloadQueries)
	{
		auto iter = world->getChunks().find(unloadQueries->data);
		Chunk *chunk = iter->second;
		world->getChunks().erase(iter);
		chunk->free();
		auto tmp = unloadQueries->next;
		delete unloadQueries;
		unloadQueries = tmp;
	}
}

void LocalServerInterface::sendInput() {

}

int LocalServerInterface::getLocalClientID() {
	return 0;
}

void LocalServerInterface::stop() {
	chunkLoader.wait();
	world->deletePlayer(0);
}

