#include "client.hpp"

#include <iostream>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <clocale>
#include <random>

#include <boost/filesystem.hpp>

#include "shared/engine/logging.hpp"
#include "shared/engine/socket.hpp"
#include "shared/engine/stopwatch.hpp"
#include "shared/engine/time.hpp"
#include "shared/engine/math.hpp"
#include "shared/game/world.hpp"
#include "shared/block_manager.hpp"
#include "shared/block_utils.hpp"
#include "shared/constants.hpp"
#include "shared/saves.hpp"
#include "gui/widget.hpp"
#include "gfx/graphics.hpp"

#include "config.hpp"
#include "menu.hpp"
#include "local_server_interface.hpp"
#include "remote_server_interface.hpp"

static logging::Logger logger("client");

int main(int argc, char *argv[]) {
	logging::init("logging.conf");
	
	LOG_INFO(logger) << "Starting client";
	LOG_TRACE(logger) << "Trace enabled";

	const char *worldId = "region";
	const char *serverAdress = nullptr;

	argv++;
	argc--;

	while (argc > 0) {
		if (strcmp(*argv, "-f") == 0) {
			worldId = *++argv;
			argc--;
		} else if (strcmp(*argv, "-a") == 0) {
			serverAdress = *++argv;
			argc--;
		}
		argv++;
		argc--;
	}

	initUtil();
	Client client(worldId, serverAdress);
	client.run();

	return 0;
}

Client::Client(const char *worldId, const char *serverAdress) {
	stopwatch = std::unique_ptr<Stopwatch>(new Stopwatch(CLOCK_ID_NUM));
	stopwatch->start(CLOCK_ALL);

	_conf = std::unique_ptr<GraphicsConf>(new GraphicsConf());
	load("graphics-default.profile", *_conf);

	save = std::unique_ptr<Save>(new Save(_conf->last_world_id.c_str()));
	boost::filesystem::path path(save->getPath());
	if (!boost::filesystem::exists(path)) {
		boost::filesystem::create_directories(path);
		std::random_device rng;
		std::uniform_int_distribution<uint64> distr;
		uint64 seed = distr(rng);
		save->initialize(_conf->last_world_id, seed);
		save->store();
	}

	blockManager = std::unique_ptr<BlockManager>(new BlockManager());
	const char *block_ids_file = "block_ids.txt";
	if (blockManager->load(block_ids_file)) {
		LOG_ERROR(logger) << "Problem loading '" << block_ids_file << "'";
	}
	LOG_INFO(logger) << blockManager->getNumberOfBlocks() << " blocks were loaded from '" << block_ids_file << "'";

	graphics = std::unique_ptr<Graphics>(new Graphics(this, &state));
	graphics->createContext();
	chunkManager = std::unique_ptr<ChunkManager>(new ChunkManager(this));
	world = std::unique_ptr<World>(new World(worldId, chunkManager.get()));
	menu = std::unique_ptr<Menu>(new Menu(this));

	if (serverAdress) {
		LOG_INFO(logger) << "Connecting to remote server '" << serverAdress << "'";
		serverInterface = std::unique_ptr<RemoteServerInterface>(new RemoteServerInterface(this, serverAdress));
	} else {
		LOG_INFO(logger) << "Connecting to local server";
		serverInterface = std::unique_ptr<LocalServerInterface>(new LocalServerInterface(this));
	}
}

Client::~Client() {
	// delete graphics so the window closes quickly
	graphics.reset();

	store("graphics-default.profile", *_conf);
}

uint8 Client::getLocalClientId() const {
	 return serverInterface->getLocalClientId();
}

Player &Client::getLocalPlayer() {
	return world->getPlayer(getLocalClientId());
}

void Client::run() {
	LOG_INFO(logger) << "Running client";
	serverInterface->dispatch();
	chunkManager->dispatch();
	time = getCurrentTime();
	int tick = 0;
	while (!closeRequested) {
		handleInput();
		chunkManager->tick();

		if (state == State::CONNECTING && serverInterface->getStatus() == ServerInterface::CONNECTED) {
			state = State::IN_MENU;
			localClientId = serverInterface->getLocalClientId();
		}

		serverInterface->tick();

		if (state == State::PLAYING || state == State::IN_MENU) {
			stopwatch->start(CLOCK_WOT);
			stopwatch->stop(CLOCK_WOT);
		}

#ifndef NO_GRAPHICS
        if (getCurrentTime() < time + timeShift + seconds(1) / TICK_SPEED) {
            graphics->tick();
        }

#endif

		// TODO finish here

		stopwatch->start(CLOCK_SYN);
		sync(TICK_SPEED);
		stopwatch->stop(CLOCK_SYN);
		tick++;
	}
	chunkManager->wait();
	chunkManager->storeChunks();
	serverInterface->wait();
}

void Client::setConf(const GraphicsConf &conf) {
	graphics->setConf(conf, *_conf);
	serverInterface->setConf(conf, *_conf);
	*_conf = conf;
}

void Client::sync(int perSecond) {
	time = time + seconds(1) / perSecond;
	sleepUntil(time + timeShift);
}

void Client::handleInput() {
	Player &player = world->getPlayer(localClientId);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_MOUSEWHEEL: {
			if (state == State::PLAYING) {
				auto block = player.getBlock();
				block += event.wheel.y;
				static const int NUMBER_OF_BLOCKS = blockManager->getNumberOfBlocks();
				while (block > NUMBER_OF_BLOCKS) {
					block -= NUMBER_OF_BLOCKS;
				}
				while (block < 1) {
					block += NUMBER_OF_BLOCKS;
				}
				serverInterface->setSelectedBlock(block);
			}
			break;
		}
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_CLOSE:
				closeRequested = true;
				break;
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				graphics->resize(
					event.window.data1,
					event.window.data2
				);
				menu->getFrame()->move(-graphics->getDrawWidth() / 2 + 10, -graphics->getDrawHeight() / 2 + 10);
				break;
			default:
				break;
			}
			break;
		case SDL_KEYDOWN:
			if (event.key.keysym.scancode == SDL_SCANCODE_O) {
				char buf[128];
				sprintf(buf, "timeShift: %ld\n", timeShift = (timeShift + 100000) % 1000000);
				LOG_INFO(logger) << buf;
			} else if (event.key.keysym.scancode == SDL_SCANCODE_P) {
				char buf[128];
				sprintf(buf, "timeShift: %ld\n", timeShift = (timeShift + 900000) % 1000000);
				LOG_INFO(logger) << buf;
			}
			if (state == State::PLAYING) {
				switch (event.key.keysym.scancode) {
				case SDL_SCANCODE_ESCAPE:
					menu->update();
					state = State::IN_MENU;
					break;
				case SDL_SCANCODE_F:
					serverInterface->toggleFly();
					break;
				case SDL_SCANCODE_M: {
					GraphicsConf conf = *_conf;
					switch (conf.aa) {
					case AntiAliasing::NONE:    conf.aa = AntiAliasing::MSAA_2; break;
					case AntiAliasing::MSAA_2:  conf.aa = AntiAliasing::MSAA_4; break;
					case AntiAliasing::MSAA_4:  conf.aa = AntiAliasing::MSAA_8; break;
					case AntiAliasing::MSAA_8:  conf.aa = AntiAliasing::MSAA_16; break;
					case AntiAliasing::MSAA_16: conf.aa = AntiAliasing::NONE; break;
					}
					setConf(conf);
					menu->update();
					break;
				}
//				case SDL_SCANCODE_N:
//					switch (graphics->getFXAA()) {
//					case true: graphics->disableFXAA(); break;
//					case false: graphics->enableFXAA(); break;
//					}
//					break;
				case SDL_SCANCODE_Q:
					if (SDL_GetModState() & KMOD_LCTRL)
						closeRequested = true;
					break;
				case SDL_SCANCODE_F11: {
					GraphicsConf conf = *_conf;
					conf.fullscreen = !conf.fullscreen;
					setConf(conf);
					break;
				}
				case SDL_SCANCODE_F3:
					_isDebugOn = !_isDebugOn;
					break;
				default:
					break;
				} // switch scancode
			} else if (state == State::IN_MENU) { // if we are in menu
				switch (event.key.keysym.scancode) {
				/*case SDL_SCANCODE_W:
					if (menu->navigateUp()) {
						graphics->setConf(*conf);
						serverInterface->setConf(*conf);
					}
					break;
				case SDL_SCANCODE_S:
					if (menu->navigateDown()) {
						graphics->setConf(*conf);
						serverInterface->setConf(*conf);
					}
				break;
				case SDL_SCANCODE_A:
					if (menu->navigateLeft()) {
						graphics->setConf(*conf);
						serverInterface->setConf(*conf);
					}
				break;
				case SDL_SCANCODE_D:
					if (menu->navigateRight()) {
						graphics->setConf(*conf);
						serverInterface->setConf(*conf);
					}
				break;*/
				case SDL_SCANCODE_ESCAPE:
					menu->apply();
					state = State::PLAYING;
					/*for (int z = -5; z < 37; z++) {
						for (int y = -5; y < 37; y++) {
							for (int x = -5; x < 37; x++) {
								world->setBlock(vec3i64(x,y,z), 0, true);
							}
						}
					}
					for (int z = 0; z < 32; z++) {
						for (int y = 0; y < 32; y++) {
							for (int x = 0; x < 32; x++) {
								world->setBlock(vec3i64(x,y,z), 1, true);
							}
						}
					}*/
					break;
				default:
					break;
				}
			}
			break;
		case SDL_MOUSEMOTION:
			if (state == State::PLAYING) {
				int yaw = player.getYaw();
				int  pitch = player.getPitch();
				yaw -= event.motion.xrel * 10.0f;
				pitch -= event.motion.yrel * 10.0f;
				yaw = cycle(yaw, 36000);
				pitch = std::max(pitch, -9000);
				pitch = std::min(pitch, 9000);
				serverInterface->setPlayerOrientation(yaw, pitch);
			} else if (state == State::IN_MENU) {
				int x = event.motion.x - graphics->getWidth() / 2;
				int y = graphics->getHeight() / 2 - event.motion.y;
				float factor = graphics->getScalingFactor();
				menu->getFrame()->updateMousePosition(x * factor, y * factor);
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			if (state == State::PLAYING) {
				vec3i64 bc;
				int d;
				bool target = player.getTargetedFace(&bc, &d);
				if (target) {
					if (event.button.button == SDL_BUTTON_LEFT) {
						vec3i64 rbc = bc + DIRS[d].cast<int64>();
						serverInterface->placeBlock(rbc, player.getBlock());
					} else if (event.button.button == SDL_BUTTON_RIGHT) {
						serverInterface->placeBlock(bc, 0);
					}
				}
			} else if (state == State::IN_MENU){
				if (event.button.button == SDL_BUTTON_LEFT) {
					int x = event.button.x - graphics->getWidth() / 2;
					int y = graphics->getHeight() / 2 - event.button.y;
					float factor = graphics->getScalingFactor();
					menu->getFrame()->handleMouseClick(x * factor, y * factor);
				}
			}
			break;
		}
	}

	const uint8 *keyboard = SDL_GetKeyboardState(nullptr);

	int moveInput = 0;
	if (state == State::PLAYING) {
		if (keyboard[SDL_SCANCODE_D])
			moveInput |= Player::MOVE_INPUT_FLAG_STRAFE_RIGHT;
		if (keyboard[SDL_SCANCODE_A])
			moveInput |= Player::MOVE_INPUT_FLAG_STRAFE_LEFT;

		if (keyboard[SDL_SCANCODE_SPACE])
			moveInput |= Player::MOVE_INPUT_FLAG_FLY_UP;
		if (keyboard[SDL_SCANCODE_LCTRL])
			moveInput |= Player::MOVE_INPUT_FLAG_FLY_DOWN;

		if (keyboard[SDL_SCANCODE_W])
			moveInput |= Player::MOVE_INPUT_FLAG_MOVE_FORWARD;
		if (keyboard[SDL_SCANCODE_S])
			moveInput |= Player::MOVE_INPUT_FLAG_MOVE_BACKWARD;

		if (keyboard[SDL_SCANCODE_LSHIFT])
			moveInput |= Player::MOVE_INPUT_FLAG_SPRINT;

		serverInterface->setPlayerMoveInput(moveInput);
	}
}
