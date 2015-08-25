#include "perlin.hpp"

#include <random>

#include "shared/engine/math.hpp"

using namespace std;

Hasher::Hasher(uint64 seed) {
    default_random_engine random((uint) seed);
    uniform_int_distribution<> distr(0x00, 0xFF);
    for (int i = 0; i < 0x100; i++) {
        p[i] = distr(random);
    }
}

Hasher &operator << (Hasher &hasher, int i) {
	hasher.feed(i);
	return hasher;
}

Perlin::Perlin(uint64 seed) : hasher(seed) {}

double Perlin::octavePerlin(double x, double y, double z, int octaves, double persistence) {
    double total = 0;
    double frequency = 1;
    double amplitude = 1;
    double max_value = 0;
    for (int i = 0; i < octaves; i++) {
        total += perlin(x * frequency, y * frequency, z * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2;
    }
    return total / max_value;
}

void Perlin::octavePerlin(
		double startX, double startY, double startZ,
		double stepSizeX, double stepSizeY, double stepSizeZ,
		uint numStepsX, uint numStepsY, uint numStepsZ,
		uint octaves, double persistence, double *buffer) {
	int index = 0;
	double x, y, z = startZ;
	for (uint zStep = 0; zStep < numStepsZ; zStep++, z += stepSizeZ) {
		y = startY;
		for (uint yStep = 0; yStep < numStepsY; yStep++, y += stepSizeY) {
			x = startX;
			for (uint xStep = 0; xStep < numStepsX; xStep++, x += stepSizeX) {
				buffer[index++] = octavePerlin(x, y, z, octaves, persistence);
			}
		}
	}
}

void Perlin::octavePerlin(
		double startX, double startY,
		double stepSizeX, double stepSizeY,
		uint numStepsX, uint numStepsY,
		uint octaves, double persistence, double *buffer) {
	int index = 0;
	double x, y = startY;
	for (uint yStep = 0; yStep < numStepsY; yStep++, y += stepSizeY) {
		x = startX;
		for (uint xStep = 0; xStep < numStepsX; xStep++, x += stepSizeX) {
			buffer[index++] = octavePerlin(x, y, 0, octaves, persistence);
		}
	}
}


double Perlin::perlin(double x, double y, double z) {
	// lowest corner of the cell, opposite corner have xi + 1 etc
    const int xi = (int) floor(x);
    const int yi = (int) floor(y);
    const int zi = (int) floor(z);

	// relative position in the cell
    const double xf = x - floor(x);
    const double yf = y - floor(y);
    const double zf = z - floor(z);

	// fade constants to smooth the solution
    const double u = fade(xf);
    const double v = fade(yf);
    const double w = fade(zf);

	// calculate pseudorandom hashes for all the corners
	hasher.reset() << xi << yi << zi;
	const int aaa = hasher.get();
	hasher.reset() << xi << yi + 1 << zi;
	const int aba = hasher.get();
	hasher.reset() << xi << yi << zi + 1;
	const int aab = hasher.get();
	hasher.reset() << xi << yi + 1 << zi + 1;
	const int abb = hasher.get();
	hasher.reset() << xi + 1 << yi << zi;
	const int baa = hasher.get();
	hasher.reset() << xi + 1 << yi + 1 << zi;
	const int bba = hasher.get();
	hasher.reset() << xi + 1 << yi << zi + 1;
	const int bab = hasher.get();
	hasher.reset() << xi + 1 << yi + 1 << zi + 1;
	const int bbb = hasher.get();

	// multiply the relative coordinate in the cell with the random gradient and lerp it together
    double x1, x2, y1, y2;
    x1 = lerp(grad(aaa, xf, yf, zf), grad(baa, xf - 1, yf, zf), u);
    x2 = lerp(grad(aba, xf, yf - 1, zf), grad(bba, xf - 1, yf - 1, zf), u);
    y1 = lerp(x1, x2, v);

    x1 = lerp(grad(aab, xf, yf, zf - 1), grad(bab, xf - 1, yf, zf - 1), u);
    x2 = lerp(grad(abb, xf, yf - 1, zf - 1), grad(bbb, xf - 1, yf - 1, zf - 1), u);
    y2 = lerp(x1, x2, v);

	// rescale the solution to fit into [0, 1]
    return (lerp(y1, y2, w) + 1) / 2;
}

double Perlin::grad(int hash, double x, double y, double z) {
	switch (hash & 0xF) {
		// 0x0 through 0xB correspond to the edges of a cube
		case 0x0: return + x + y;
		case 0x1: return - x + y;
		case 0x2: return + x - y;
		case 0x3: return - x - y;
		case 0x4: return + x + z;
		case 0x5: return - x + z;
		case 0x6: return + x - z;
		case 0x7: return - x - z;
		case 0x8: return + y + z;
		case 0x9: return - y + z;
		case 0xA: return + y - z;
		case 0xB: return - y - z;
		// these duplicates give some speed in exchange for uniformity
		case 0xC: return + y + x;
		case 0xD: return - y + z;
		case 0xE: return + y - x;
		case 0xF: return - y - z;
		// can never happen
		default: return 0.0;
	}
}

double Perlin::fade(double t) {
	// 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double Perlin::lerp(double a, double b, double x) {
	// linear interpolation
    return a + x * (b - a);
}
