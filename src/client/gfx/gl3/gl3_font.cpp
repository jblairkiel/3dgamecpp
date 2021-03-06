/*
	AngelCode Tool Box Library
	Copyright (c) 2007-2008 Andreas Jönsson
  
	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you
	   must not claim that you wrote the original software. If you use
	   this software in a product, an acknowledgment in the product
	   documentation would be appreciated but is not required.

	2. Altered source versions must be plainly marked as such, and
	   must not be misrepresented as being the original software.

	3. This notice may not be removed or altered from any source
	   distribution.
  
	Andreas Jönsson
	andreas@angelcode.com
*/

/*
	This file was modified from its original version!
	The original version could be obtained from
		http://www.angelcode.com/dev/bmfonts/
	as of 2015-03-22
	The original filename was acgfx_font.cpp

	2015-03-22 - Lars Thorben Drögemüller
		Removed enclosing namespace and enforced coding conventions
		All the DirectX specific code was thrown out
		Rewrote rendering code in OpenGL 3.3
		Seperated file format logic from rendering logic by subclassing
*/

#include "gl3_font.hpp"

#define GLM_FORCE_RADIANS

#include <fstream>

#include <SDL2/SDL_image.h>
#include <GL/glew.h>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "shared/engine/macros.hpp"
#include "shared/engine/logging.hpp"
#include "shared/engine/unicode_int.hpp"

static logging::Logger logger("gfx");

class BMFontLoader {
public:
	BMFontLoader(FILE *f, BMFont *font, const char *fontFile);
	virtual ~BMFontLoader() = default;

	virtual int Load() = 0; // Must be implemented by derived class

protected:
	void LoadPage(int id, const char *pageFile, const char *fontFile);
	void SetFontInfo(int outlineThickness);
	void SetCommonInfo(short fontHeight, short base, short scaleW, short scaleH, short pages, bool isPacked);
	void AddChar(int id, short x, short y, short w, short h, short xoffset, short yoffset, short xadvance, short page, unsigned int chnl);
	void AddKerningPair(int first, int second, int amount);

	FILE *f = nullptr;
	BMFont *font = nullptr;
	const char *fontFile = nullptr;

	int outlineThickness = 0;
};

class BMFontLoaderTextFormat : public BMFontLoader {
public:
	BMFontLoaderTextFormat(FILE *f, BMFont *font, const char *fontFile);

	int Load();

	int SkipWhiteSpace(std::string &str, int start);
	int FindEndOfToken(std::string &str, int start);

	void InterpretInfo(std::string &str, int start);
	void InterpretCommon(std::string &str, int start);
	void InterpretChar(std::string &str, int start);
	void InterpretSpacing(std::string &str, int start);
	void InterpretKerning(std::string &str, int start);
	void InterpretPage(std::string &str, int start, const char *fontFile);
};

class BMFontLoaderBinaryFormat : public BMFontLoader {
public:
	BMFontLoaderBinaryFormat(FILE *f, BMFont *font, const char *fontFile);

	int Load();

	void ReadInfoBlock(int size);
	void ReadCommonBlock(int size);
	void ReadPagesBlock(int size);
	void ReadCharsBlock(int size);
	void ReadKerningPairsBlock(int size);
};

BMFont::BMFont(FontShader *shader) :
	shader(shader)
{
	// nothing
}

BMFont::~BMFont()
{
	std::map<int, CharDesc*>::iterator it = chars.begin();
	while (it != chars.end()) {
		delete it->second;
		it++;
	}

	GL(DeleteTextures(1, &tex));
	GL(DeleteProgram(program));
}

int BMFont::load(const char *fontFile) {
	// Load the font
	FILE *f = fopen(fontFile, "rb");

	// Determine format by reading the first bytes of the file
	char str[4] = { 0 };
	fread(str, 3, 1, f);
	fseek(f, 0, SEEK_SET);

	BMFontLoader *loader = 0;
	if (strcmp(str, "BMF") == 0)
		loader = new BMFontLoaderBinaryFormat(f, this, fontFile);
	else
		loader = new BMFontLoaderTextFormat(f, this, fontFile);

	auto r = loader->Load();
	delete loader;

	// build vbo for all glyphs
	GL(BindVertexArray(0));
	GL(GenBuffers(1, &vbo));
	GL(BindBuffer(GL_ARRAY_BUFFER, vbo));
	GL(BufferData(GL_ARRAY_BUFFER, 0, 0, GL_STATIC_DRAW));

	// build huge fucking array here
	size_t bufferSize = (chars.size() + 1) * 6 * 4;
	float *vboBuffer = new float[bufferSize];
	float *head = vboBuffer;

	auto buildCharVBOLambda = [&] (CharDesc *desc) {
		desc->vboOffset = (uint)(head - vboBuffer) * sizeof(float);

		*head++ = 0.0f;
		*head++ = 0.0f;
		*head++ = (float)desc->srcX / (float)scaleW;
		*head++ = (float)(desc->srcY + desc->srcH) / (float)scaleH;

		*head++ = (float)desc->srcW;
		*head++ = 0.0f;
		*head++ = (float)(desc->srcX + desc->srcW) / (float)scaleW;
		*head++ = (float)(desc->srcY + desc->srcH) / (float)scaleH;

		*head++ = (float)desc->srcW;
		*head++ = (float)desc->srcH;
		*head++ = (float)(desc->srcX + desc->srcW) / (float)scaleW;
		*head++ = (float)desc->srcY / (float)scaleH;

		*head++ = (float)desc->srcW;
		*head++ = (float)desc->srcH;
		*head++ = (float)(desc->srcX + desc->srcW) / (float)scaleW;
		*head++ = (float)desc->srcY / (float)scaleH;

		*head++ = 0.0f;
		*head++ = (float)desc->srcH;
		*head++ = (float)desc->srcX / (float)scaleW;
		*head++ = (float)desc->srcY / (float)scaleH;

		*head++ = 0.0f;
		*head++ = 0.0f;
		*head++ = (float)desc->srcX / (float)scaleW;
		*head++ = (float)(desc->srcY + desc->srcH) / (float)scaleH;
	};

	buildCharVBOLambda(&defChar);

	for (auto &descPair : chars) {
		auto desc = descPair.second;
		buildCharVBOLambda(desc);
	}

	GL(BufferData(GL_ARRAY_BUFFER, bufferSize * sizeof(float), vboBuffer, GL_STATIC_DRAW));

	delete[] vboBuffer;

	return r;
}

float BMFont::getKerning(int first, int second)
{
	CharDesc *ch = getChar(first);
	if (ch == 0) return 0;
	for (uint n = 0; n < ch->kerningPairs.size(); n += 2) {
		if (ch->kerningPairs[n] == second)
			return ch->kerningPairs[n + 1] * scale;
	}

	return 0;
}

float BMFont::getTextWidth(const char *text, int count) {
	if (count <= 0)
		count = getTextLength(text);

	float x = 0;

	for (int n = 0; n < count;) {
		int charId = getTextChar(text, n, &n);

		CharDesc *ch = getChar(charId);
		if (ch == 0) ch = &defChar;

		x += scale * (ch->xAdv);

		if (n < count)
			x += getKerning(charId, getTextChar(text, n));
	}

	return x;
}

void BMFont::setHeight(float h)
{
	scale = h / float(lineHeight);
}

float BMFont::getBottomOffset()
{
	return scale * (base - lineHeight);
}

float BMFont::getTopOffset()
{
	return scale * (base - 0);
}

void BMFont::beginRender() {
	GL(BindVertexArray(0));
	GL(BindBuffer(GL_ARRAY_BUFFER, vbo));
	GL(EnableVertexAttribArray(0)); // coord
	GL(EnableVertexAttribArray(1)); // texCoord
	GL(ActiveTexture(GL_TEXTURE0));
	GL(BindTexture(GL_TEXTURE_2D_ARRAY, tex));
	shader->setIsPacked(isPacked);
	shader->setHasOutline(hasOutline);
	shader->setTextColor(textColor);
	shader->setOutlineColor(outlineColor);
}

float BMFont::renderGlyph(float x, float y, float z, int glyph) {
	z++; // to get rid of unused warning
	y += scale * float(base);

	CharDesc *ch = getChar(glyph);
	if (ch == 0) ch = &defChar;

	float a = scale * float(ch->xAdv);
	//float w = scale * float(ch->srcW); //unused
	float h = scale * float(ch->srcH);
	float ox = scale * float(ch->xOff);
	float oy = scale * float(ch->yOff);

	void *coordOffset = reinterpret_cast<void *>(ch->vboOffset);
	void *texCoordOffset = (void *)(ch->vboOffset + 2 * sizeof(float));
	GL(VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), coordOffset));
	GL(VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), texCoordOffset));

	glm::mat4 ident(1.0f);
	auto transl = glm::vec3(x + ox, y - (h + oy), 0.0f);
	auto modelMat = glm::translate(ident, transl);
	shader->setModelMatrix(modelMat);
	shader->setPage(ch->page);
	shader->setChannel(ch->chnl);

	shader->useProgram();
	GL(DrawArrays(GL_TRIANGLES, 0, 6));

	return a;
}

BMFont::CharDesc *BMFont::getChar(int id)
{
	std::map<int, CharDesc*>::iterator it = chars.find(id);
	if (it == chars.end())
		return nullptr;
	return it->second;
}

void BMFont::writeInternal(float x, float y, float z, const char *text, int count, float spacing) {
	if (hasOutline && outline) {
		shader->setMode(FontShader::FontRenderMode::OUTLINE);
		Font::writeInternal(x, y, z, text, count, spacing);
	}
	shader->setMode(FontShader::FontRenderMode::TEXT);
	Font::writeInternal(x, y, z, text, count, spacing);
}

//=============================================================================
// BMFontLoader
//
// This is the base class for all loader classes. This is the only class
// that has access to and knows how to set the BMFont members.
//=============================================================================

using namespace std;

BMFontLoader::BMFontLoader(FILE *f, BMFont *font, const char *fontFile)
{
	this->f = f;
	this->font = font;
	this->fontFile = fontFile;

	outlineThickness = 0;
}

void BMFontLoader::LoadPage(int id, const char *pageFile, const char *fontFile)
{
	string str;
	SDL_Surface *img = nullptr, *tmp = nullptr;

	// Load the texture from the same directory as the font descriptor file
	str = fontFile;
	for (size_t n = 0; (n = str.find('\\', n)) != string::npos;) str.replace(n, 1, "/");
	size_t i = str.rfind('/');
	if (i != string::npos)
		str = str.substr(0, i + 1);
	else
		str = "";
	str += pageFile;

	// Load the font textures
	img = IMG_Load(str.c_str());
	if (!img) {
		LOG_ERROR(logger) << "Textures could not be loaded";
		goto FAILURE;
	}

	SDL_PixelFormat pixelFormat;
	pixelFormat.format = SDL_PIXELFORMAT_RGBA8888;
	pixelFormat.palette = nullptr;
	pixelFormat.BitsPerPixel = 32;
	pixelFormat.BytesPerPixel = 4;
	pixelFormat.Rmask = 0x000000FF;
	pixelFormat.Gmask = 0x0000FF00;
	pixelFormat.Bmask = 0x00FF0000;
	pixelFormat.Amask = 0xFF000000;
	tmp = SDL_ConvertSurface(img, &pixelFormat, 0);
	if (!tmp) {
		LOG_ERROR(logger) << "Temporary SDL_Surface could not be created";
		goto FAILURE;
	}

	GL(BindTexture(GL_TEXTURE_2D_ARRAY, font->tex));
	GL(TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, id, font->scaleW, font->scaleH, 1, GL_RGBA, GL_UNSIGNED_BYTE, tmp->pixels));
	SDL_FreeSurface(img);
	SDL_FreeSurface(tmp);
	return;

FAILURE:
	{
		LOG_ERROR(logger) << "Failed to load font page '" << str.c_str() << "'";
		if (img) SDL_FreeSurface(img);
		if (tmp) SDL_FreeSurface(tmp);
		return;
	}
}

void BMFontLoader::SetFontInfo(int outlineThickness)
{
	this->outlineThickness = outlineThickness;
}

void BMFontLoader::SetCommonInfo(short lineHeight, short base, short scaleW, short scaleH, short pages, bool isPacked)
{
	font->lineHeight = lineHeight;
	font->base = base;
	font->scaleW = scaleW;
	font->scaleH = scaleH;
	font->pages = pages;
	font->isPacked = isPacked;
	if (isPacked && outlineThickness) {
		font->hasOutline = true;
		font->outline = true;
	}

	GL(GenTextures(1, &font->tex));
	GL(BindTexture(GL_TEXTURE_2D_ARRAY, font->tex));
	GL(TexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, scaleW, scaleH, pages));

	GL(TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL(TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
}

void BMFontLoader::AddChar(int id, short x, short y, short w, short h, short xoffset, short yoffset, short xadvance, short page, unsigned int chnl)
{
	// Convert to a 4 element vector
	// TODO: Does this depend on hardware? It probably does
	if (chnl == 1) chnl = 2;  // Blue channel
	else if (chnl == 2) chnl = 1;  // Green channel
	else if (chnl == 4) chnl = 0;  // Red channel
	else if (chnl == 8) chnl = 3;  // Alpha channel
	else chnl = -1;

	if (id >= 0) {
		BMFont::CharDesc *ch = new BMFont::CharDesc{
			x, y, w, h, xoffset, yoffset, xadvance, page, chnl, 0, std::vector<int>()
		};
		font->chars.insert(std::map<int, BMFont::CharDesc*>::value_type(id, ch));
	}

	if (id == -1) {
		font->defChar = BMFont::CharDesc{
			x, y, w, h, xoffset, yoffset, xadvance, page, chnl, 0, std::vector<int>()
		};
	}
}

void BMFontLoader::AddKerningPair(int first, int second, int amount)
{
	if (first >= 0 && first < 256 && font->chars[first]) {
		font->chars[first]->kerningPairs.push_back(second);
		font->chars[first]->kerningPairs.push_back(amount);
	}
}

//=============================================================================
// CFontLoaderTextFormat
//
// This class implements the logic for loading a BMFont file in text format
//=============================================================================

BMFontLoaderTextFormat::BMFontLoaderTextFormat(FILE *f, BMFont *font, const char *fontFile) : BMFontLoader(f, font, fontFile)
{
	// nothing
}

int BMFontLoaderTextFormat::Load()
{
	string line;

	while (!feof(f)) {
		// Read until line feed (or EOF)
		line = "";
		line.reserve(256);
		while (!feof(f)) {
			char ch;
			if (fread(&ch, 1, 1, f)) {
				if (ch != '\n')
					line += ch;
				else
					break;
			}
		}

		// Skip white spaces
		int pos = SkipWhiteSpace(line, 0);

		// Read token
		int pos2 = FindEndOfToken(line, pos);
		string token = line.substr(pos, pos2 - pos);

		// Interpret line
		if (token == "info")
			InterpretInfo(line, pos2);
		else if (token == "common")
			InterpretCommon(line, pos2);
		else if (token == "char")
			InterpretChar(line, pos2);
		else if (token == "kerning")
			InterpretKerning(line, pos2);
		else if (token == "page")
			InterpretPage(line, pos2, fontFile);
	}

	fclose(f);

	// Success
	return 0;
}

int BMFontLoaderTextFormat::SkipWhiteSpace(string &str, int start)
{
	uint n = start;
	while (n < str.size()) {
		char ch = str[n];
		if (ch != ' ' &&
			ch != '\t' &&
			ch != '\r' &&
			ch != '\n')
			break;

		++n;
	}

	return n;
}

int BMFontLoaderTextFormat::FindEndOfToken(string &str, int start)
{
	uint n = start;
	if (str[n] == '"') {
		n++;
		while (n < str.size()) {
			char ch = str[n];
			if (ch == '"') {
				// Include the last quote char in the token
				++n;
				break;
			}
			++n;
		}
	} else {
		while (n < str.size()) {
			char ch = str[n];
			if (ch == ' ' ||
				ch == '\t' ||
				ch == '\r' ||
				ch == '\n' ||
				ch == '=')
				break;

			++n;
		}
	}

	return n;
}

void BMFontLoaderTextFormat::InterpretKerning(string &str, int start)
{
	// Read the attributes
	int first = 0;
	int second = 0;
	int amount = 0;

	unsigned int pos, pos2 = start;
	while (true) {
		pos = SkipWhiteSpace(str, pos2);
		pos2 = FindEndOfToken(str, pos);

		string token = str.substr(pos, pos2 - pos);

		pos = SkipWhiteSpace(str, pos2);
		if (pos == str.size() || str[pos] != '=') break;

		pos = SkipWhiteSpace(str, pos + 1);
		pos2 = FindEndOfToken(str, pos);

		string value = str.substr(pos, pos2 - pos);

		if (token == "first")
			first = strtol(value.c_str(), 0, 10);
		else if (token == "second")
			second = strtol(value.c_str(), 0, 10);
		else if (token == "amount")
			amount = strtol(value.c_str(), 0, 10);

		if (pos == str.size()) break;
	}

	// Store the attributes
	AddKerningPair(first, second, amount);
}

void BMFontLoaderTextFormat::InterpretChar(string &str, int start)
{
	// Read all attributes
	int id = 0;
	short x = 0;
	short y = 0;
	short width = 0;
	short height = 0;
	short xoffset = 0;
	short yoffset = 0;
	short xadvance = 0;
	short page = 0;
	unsigned short chnl = 0;

	unsigned int pos, pos2 = start;
	while (true) {
		pos = SkipWhiteSpace(str, pos2);
		pos2 = FindEndOfToken(str, pos);

		string token = str.substr(pos, pos2 - pos);

		pos = SkipWhiteSpace(str, pos2);
		if (pos == str.size() || str[pos] != '=') break;

		pos = SkipWhiteSpace(str, pos + 1);
		pos2 = FindEndOfToken(str, pos);

		string value = str.substr(pos, pos2 - pos);

		if (token == "id")
			id = (int) strtol(value.c_str(), 0, 10);
		else if (token == "x")
			x = (short) strtol(value.c_str(), 0, 10);
		else if (token == "y")
			y = (short) strtol(value.c_str(), 0, 10);
		else if (token == "width")
			width = (short) strtol(value.c_str(), 0, 10);
		else if (token == "height")
			height = (short) strtol(value.c_str(), 0, 10);
		else if (token == "xoffset")
			xoffset = (short) strtol(value.c_str(), 0, 10);
		else if (token == "yoffset")
			yoffset = (short) strtol(value.c_str(), 0, 10);
		else if (token == "xadvance")
			xadvance = (short) strtol(value.c_str(), 0, 10);
		else if (token == "page")
			page = (short) strtol(value.c_str(), 0, 10);
		else if (token == "chnl")
			chnl = (unsigned int) strtol(value.c_str(), 0, 10);

		if (pos == str.size()) break;
	}

	// Store the attributes
	AddChar(id, x, y, width, height, xoffset, yoffset, xadvance, page, chnl);
}

void BMFontLoaderTextFormat::InterpretCommon(string &str, int start)
{
	short fontHeight = 0;
	short base = 0;
	short scaleW = 0;
	short scaleH = 0;
	short pages = 0;
	bool packed = false;

	// Read all attributes
	unsigned int pos, pos2 = start;
	while (true) {
		pos = SkipWhiteSpace(str, pos2);
		pos2 = FindEndOfToken(str, pos);

		string token = str.substr(pos, pos2 - pos);

		pos = SkipWhiteSpace(str, pos2);
		if (pos == str.size() || str[pos] != '=') break;

		pos = SkipWhiteSpace(str, pos + 1);
		pos2 = FindEndOfToken(str, pos);

		string value = str.substr(pos, pos2 - pos);

		if (token == "lineHeight")
			fontHeight = (short) strtol(value.c_str(), 0, 10);
		else if (token == "base")
			base = (short) strtol(value.c_str(), 0, 10);
		else if (token == "scaleW")
			scaleW = (short) strtol(value.c_str(), 0, 10);
		else if (token == "scaleH")
			scaleH = (short) strtol(value.c_str(), 0, 10);
		else if (token == "pages")
			pages = (short) strtol(value.c_str(), 0, 10);
		else if (token == "packed")
			packed = strtol(value.c_str(), 0, 10) != 0;

		if (pos == str.size()) break;
	}

	SetCommonInfo(fontHeight, base, scaleW, scaleH, pages, packed);
}

void BMFontLoaderTextFormat::InterpretInfo(string &str, int start)
{
	int outlineThickness = 0;

	// Read all attributes
	unsigned int pos, pos2 = start;
	while (true) {
		pos = SkipWhiteSpace(str, pos2);
		pos2 = FindEndOfToken(str, pos);

		string token = str.substr(pos, pos2 - pos);

		pos = SkipWhiteSpace(str, pos2);
		if (pos == str.size() || str[pos] != '=') break;

		pos = SkipWhiteSpace(str, pos + 1);
		pos2 = FindEndOfToken(str, pos);

		string value = str.substr(pos, pos2 - pos);

		if (token == "outline")
			outlineThickness = (short)strtol(value.c_str(), 0, 10);

		if (pos == str.size()) break;
	}

	SetFontInfo(outlineThickness);
}

void BMFontLoaderTextFormat::InterpretPage(string &str, int start, const char *fontFile)
{
	int id = 0;
	string file;

	// Read all attributes
	unsigned int pos, pos2 = start;
	while (true) {
		pos = SkipWhiteSpace(str, pos2);
		pos2 = FindEndOfToken(str, pos);

		string token = str.substr(pos, pos2 - pos);

		pos = SkipWhiteSpace(str, pos2);
		if (pos == str.size() || str[pos] != '=') break;

		pos = SkipWhiteSpace(str, pos + 1);
		pos2 = FindEndOfToken(str, pos);

		string value = str.substr(pos, pos2 - pos);

		if (token == "id")
			id = strtol(value.c_str(), 0, 10);
		else if (token == "file")
			file = value.substr(1, value.length() - 2);

		if (pos == str.size()) break;
	}

	LoadPage(id, file.c_str(), fontFile);
}

//=============================================================================
// BMFontLoaderBinaryFormat
//
// This class implements the logic for loading a BMFont file in binary format
//=============================================================================

BMFontLoaderBinaryFormat::BMFontLoaderBinaryFormat(FILE *f, BMFont *font, const char *fontFile) : BMFontLoader(f, font, fontFile)
{
	// nothing
}

int BMFontLoaderBinaryFormat::Load()
{
	// Read and validate the tag. It should be 66, 77, 70, 2,
	// or 'BMF' and 2 where the number is the file version.
	char magicString[4];
	fread(magicString, 4, 1, f);
	if (strncmp(magicString, "BMF\003", 4) != 0) {
		//LOG(("Unrecognized format for '%s'", fontFile));
		fclose(f);
		return -1;
	}

	// Read each block
	char blockType;
	int blockSize;
	while (fread(&blockType, 1, 1, f)) {
		// Read the blockSize
		fread(&blockSize, 4, 1, f);

		switch (blockType) {
		case 1: // info
			ReadInfoBlock(blockSize);
			break;
		case 2: // common
			ReadCommonBlock(blockSize);
			break;
		case 3: // pages
			ReadPagesBlock(blockSize);
			break;
		case 4: // chars
			ReadCharsBlock(blockSize);
			break;
		case 5: // kerning pairs
			ReadKerningPairsBlock(blockSize);
			break;
		default:
			//LOG(("Unexpected block type (%d)", blockType));
			fclose(f);
			return -1;
		}
	}

	fclose(f);

	// Success
	return 0;
}

void BMFontLoaderBinaryFormat::ReadInfoBlock(int size)
{
	PACKED(
	struct infoBlock {
		uint16 fontSize;
		uint   reserved : 4;
		uint   bold : 1;
		uint   italic : 1;
		uint   unicode : 1;
		uint   smooth : 1;
		uint8  charSet;
		uint16 stretchH;
		uint8  aa;
		uint8  paddingUp;
		uint8  paddingRight;
		uint8  paddingDown;
		uint8  paddingLeft;
		uint8  spacingHoriz;
		uint8  spacingVert;
		uint8  outline; // Added with version 2
		char fontName[1];
	});

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	// We're only interested in the outline thickness
	infoBlock *blk = (infoBlock*)buffer;
	SetFontInfo(blk->outline);

	delete[] buffer;
}

void BMFontLoaderBinaryFormat::ReadCommonBlock(int size)
{

	PACKED(
	struct commonBlock {
		uint16 lineHeight;
		uint16 base;
		uint16 scaleW;
		uint16 scaleH;
		uint16 pages;
		uint8 packed : 1;
		uint8 reserved : 7;
		uint8 alphaChnl;
		uint8 redChnl;
		uint8 greenChnl;
		uint8 blueChnl;
	});

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	commonBlock *blk = (commonBlock*)buffer;

	SetCommonInfo(blk->lineHeight, blk->base, blk->scaleW, blk->scaleH, blk->pages, blk->packed ? true : false);

	delete[] buffer;
}

void BMFontLoaderBinaryFormat::ReadPagesBlock(int size)
{

	PACKED(
	struct pagesBlock {
		char pageNames[1];
	});

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	pagesBlock *blk = (pagesBlock*)buffer;

	for (int id = 0, pos = 0; pos < size; id++) {
		LoadPage(id, &blk->pageNames[pos], fontFile);
		pos += 1 + (int)strlen(&blk->pageNames[pos]);
	}

	delete[] buffer;
}

void BMFontLoaderBinaryFormat::ReadCharsBlock(int size)
{

	PACKED(
	struct charsBlock {
		struct charInfo {
			uint32 id;
			uint16 x;
			uint16 y;
			uint16 width;
			uint16 height;
			int16  xoffset;
			int16  yoffset;
			int16  xadvance;
			uint8  page;
			uint8  chnl;
		} chars[1];
	});

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	charsBlock *blk = (charsBlock*)buffer;

	for (int n = 0; int(n*sizeof(charsBlock::charInfo)) < size; n++) {
		AddChar(blk->chars[n].id,
			blk->chars[n].x,
			blk->chars[n].y,
			blk->chars[n].width,
			blk->chars[n].height,
			blk->chars[n].xoffset,
			blk->chars[n].yoffset,
			blk->chars[n].xadvance,
			blk->chars[n].page,
			blk->chars[n].chnl);
	}

	delete[] buffer;
}

void BMFontLoaderBinaryFormat::ReadKerningPairsBlock(int size)
{
	PACKED(
	struct kerningPairsBlock {
		struct kerningPair {
			uint32 first;
			uint32 second;
			int16  amount;
		} kerningPairs[1];
	});

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	kerningPairsBlock *blk = (kerningPairsBlock*)buffer;

	for (int n = 0; int(n*sizeof(kerningPairsBlock::kerningPair)) < size; n++) {
		AddKerningPair(blk->kerningPairs[n].first,
			blk->kerningPairs[n].second,
			blk->kerningPairs[n].amount);
	}

	delete[] buffer;
}

// 2008-05-11 Storing the characters in a map instead of an array
// 2008-05-11 Loading the new binary format for BMFont v1.10
// 2008-05-17 Added support for writing text with UTF8 and UTF16 encoding
