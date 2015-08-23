#include "chunk_manager.hpp"
#include "engine/logging.hpp"
#include "engine/time.hpp"
#include "client/client.hpp"
#include "client/server_interface.hpp"
#include "game/world.hpp"

using namespace std;

static logging::Logger logger("chm");

ChunkManager::ChunkManager(Client *client) :
	loadedStoredQueue(1024),
	toLoadStoreQueue(1024),
	chunks(0, vec3i64HashFunc),
	oldRevisions(0, vec3i64HashFunc),
	needCounter(0, vec3i64HashFunc),
	client(client),
	archive("./region/")
{
	for (int i = 0; i < CHUNK_POOL_SIZE; i++) {
		chunkPool[i] = new Chunk(Chunk::ChunkFlags::VISUAL);
		unusedChunks.push(chunkPool[i]);
	}
}

ChunkManager::~ChunkManager() {
	for (int i = 0; i < CHUNK_POOL_SIZE; i++) {
		delete chunkPool[i];
	}
}

void ChunkManager::tick() {
	while (!requestedQueue.empty() && !unusedChunks.empty()) {
		vec3i64 cc = requestedQueue.front();
		Chunk *chunk = unusedChunks.top();
		chunk->initCC(cc);
		if (!chunk)
			LOG_ERROR(logger) << "Chunk allocation failed";
		ArchiveOperation op = {chunk, LOAD};
		if (toLoadStoreQueue.push(op)) {
			requestedQueue.pop();
			unusedChunks.pop();
		} else
			break;
	}

	while (!preToStoreQueue.empty()) {
		Chunk *chunk = preToStoreQueue.front();
		ArchiveOperation op = {chunk, STORE};
		if (toLoadStoreQueue.push(op)) {
			preToStoreQueue.pop();
			continue;
		} else
			break;
	}

	while(!notInCacheQueue.empty()) {
		Chunk *chunk = notInCacheQueue.front();
		if (client->getServerInterface()->requestChunk(chunk))
			notInCacheQueue.pop();
		else
			break;
	}

	ArchiveOperation op;
	while (loadedStoredQueue.pop(op)) {
		switch(op.type) {
		case LOAD:
			if (op.chunk->isInitialized()) {
				if (insertLoadedChunk(op.chunk))
					oldRevisions.insert({op.chunk->getCC(), op.chunk->getRevision()});
			} else {
				notInCacheQueue.push(op.chunk);
			}
			numSessionChunkLoads++;
			break;
		case STORE:
			recycleChunk(op.chunk);
			break;
		}
	}

	Chunk *chunk;
	while ((chunk = client->getServerInterface()->getNextChunk()) != nullptr) {
		if (!chunk->isInitialized())
			LOG_WARNING(logger) << "Server interface didn't initialize chunk";
		insertLoadedChunk(chunk);
		numSessionChunkGens++;
	}
}

void ChunkManager::doWork() {
	ArchiveOperation op;
	if (toLoadStoreQueue.pop(op)) {
		switch (op.type) {
		case LOAD:
			archive.loadChunk(op.chunk);
			break;
		case STORE:
			archive.storeChunk(*op.chunk);
			break;
		}
		while (!loadedStoredQueue.push(op)) {
			sleepFor(millis(50));
		}
	} else {
		sleepFor(millis(100));
	}
}

void ChunkManager::onStop() {
	ArchiveOperation op;
	while (toLoadStoreQueue.pop(op)) {
		if (op.type == STORE) {
			archive.storeChunk(*op.chunk);
		}
	}
}

void ChunkManager::storeChunks() {
	wait();
	for (auto it = chunks.begin(); it != chunks.end(); ++it) {
		archive.storeChunk(*it->second);
	}
}

void ChunkManager::placeBlock(vec3i64 chunkCoords, size_t intraChunkIndex,
		uint blockType, uint32 revision) {
	auto it = chunks.find(chunkCoords);
	if (it != chunks.end()) {
		if (it->second->getRevision() == revision)
			it->second->setBlock(intraChunkIndex, blockType);
		else
			LOG_WARNING(logger) << "couldn't apply chunk patch";
	}
	// TODO operate on cache if chunk is not loaded
}

const Chunk *ChunkManager::getChunk(vec3i64 chunkCoords) const {
	auto it = chunks.find(chunkCoords);
	if (it != chunks.end())
		return it->second;
	return nullptr;
}

void ChunkManager::requestChunk(vec3i64 chunkCoords) {
	auto it = needCounter.find(chunkCoords);
	if (it == needCounter.end()) {
		requestedQueue.push(chunkCoords);
		needCounter.insert({chunkCoords, 1});
	} else {
		it->second++;
	}
}

void ChunkManager::releaseChunk(vec3i64 chunkCoords) {
	auto it1 = needCounter.find(chunkCoords);
	if (it1 != needCounter.end()) {
		it1->second--;
		if (it1->second == 0) {
			needCounter.erase(it1);
			auto it2 = chunks.find(chunkCoords);
			if (it2 != chunks.end()) {
				auto it3 = oldRevisions.find(chunkCoords);
				if (it3 == oldRevisions.end() || it2->second->getRevision() != it3->second)
					preToStoreQueue.push(it2->second);
				else
					recycleChunk(it2->second);
				chunks.erase(it2);
				if (it3 != oldRevisions.end())
					oldRevisions.erase(it3);
			}
		}
	}
}

int ChunkManager::getNumNeededChunks() const {
	return needCounter.size();
}

int ChunkManager::getNumAllocatedChunks() const {
	return CHUNK_POOL_SIZE - unusedChunks.size();
}

int ChunkManager::getNumLoadedChunks() const {
	return chunks.size();
}

int ChunkManager::getRequestedQueueSize() const {
	return requestedQueue.size();
}

int ChunkManager::getNotInCacheQueueSize() const {
	return notInCacheQueue.size();
}

bool ChunkManager::insertLoadedChunk(Chunk *chunk) {
	auto it = needCounter.find(chunk->getCC());
	if (it != needCounter.end()) {
		chunks.insert({chunk->getCC(), chunk});
		return true;
	} else {
		recycleChunk(chunk);
		return false;
	}
}

void ChunkManager::recycleChunk(Chunk *chunk) {
	chunk->reset();
	unusedChunks.push(chunk);
}
