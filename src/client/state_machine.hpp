#ifndef STATE_MACHINE_HPP_
#define STATE_MACHINE_HPP_

#include <vector>

#include "shared/engine/std_types.hpp"

class State;

class StateMachine {
public:
	~StateMachine();

	void push(State *);
	void pop();

	/// gets a specific state, counting from the bottom
	State *get(int i);
	/// gets the top most state
	State *top();
	/// gets the number of states on the stack
	size_t size();

	void update();

private:
	std::vector<State *> stack;
};

#endif // STATE_MACHINE_HPP_
