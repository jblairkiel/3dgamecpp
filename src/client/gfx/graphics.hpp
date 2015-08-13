#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#include <memory>

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include "shared/game/world.hpp"
#include "shared/engine/stopwatch.hpp"

#include "client/client.hpp"
#include "client/menu.hpp"
#include "client/config.hpp"

#include "renderer.hpp"

namespace gui {
	class Frame;
	class Label;
	class Widget;
	class Button;
}

class Graphics {
private:
	Client *client = nullptr;

	SDL_GLContext glContext;
	SDL_Window *window;
	std::unique_ptr<Renderer> renderer;

	const Client::State &state;
	Client::State oldState;

	int width;
	int height;

	float drawWidth;
	float drawHeight;

	// for saving mouse position in menu
	float oldRelMouseX = 0.5;
	float oldRelMouseY = 0.5;

public:
	Graphics(Client *client, const Client::State *state);
	~Graphics();

	Renderer *getRenderer() const { return renderer.get(); }

	bool createContext();
	bool createGL2Context();
	bool createGL3Context();

	void resize(int width, int height);
	void setConf(const GraphicsConf &, const GraphicsConf &);

	int getHeight() const;
	int getWidth() const;

	float getDrawHeight() const;
	float getDrawWidth() const;

	float getScalingFactor() const;

	void tick();
	void flip();

private:
	void calcDrawArea();
	void setMenu(bool menuActive);
};

#endif // GRAPHICS_HPP