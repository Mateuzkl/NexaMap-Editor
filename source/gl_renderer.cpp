#include <glad/glad.h>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
#elif defined(__APPLE__)
	#include <dlfcn.h>
#else
	#include <dlfcn.h>
#endif

#include "main.h"
#include "gl_renderer.h"
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

#ifdef _WIN32
static void* rmeGetGLProc(const char* name) {
	auto p = (void*)wglGetProcAddress(name);
	if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1) {
		static HMODULE gl = LoadLibraryA("opengl32.dll");
		p = (void*)GetProcAddress(gl, name);
	}
	return p;
}
#elif defined(__APPLE__)
static void* rmeGetGLProc(const char* name) {
	static void* lib = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
	return lib ? dlsym(lib, name) : nullptr;
}
#else
typedef void (*__GLXextFuncPtr)(void);
extern "C" __GLXextFuncPtr glXGetProcAddressARB(const unsigned char*);
static void* rmeGetGLProc(const char* name) {
	void* p = (void*)glXGetProcAddressARB((const GLubyte*)name);
	if (!p) {
		static void* lib = dlopen("libGL.so.1", RTLD_LAZY);
		if (lib) {
			p = dlsym(lib, name);
		}
	}
	return p;
}
#endif

std::vector<GLRenderer*> GLRenderer::s_instances;

static const char* const vertSrc = R"(
#version 330
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main(){
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vUV = aUV;
	vColor = aColor;
}
)";

static const char* const fragSrc = R"(
#version 330
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
uniform int uUseTexture;
uniform int uStipple;
out vec4 FragColor;
void main() {
    if (uStipple != 0) {
        float p = gl_FragCoord.x + gl_FragCoord.y;
        if (mod(p, 4.0) < 2.0) discard;
    }
    if (uUseTexture != 0)
        FragColor = texture(uTexture, vUV) * vColor;
    else
        FragColor = vColor;
}
)";

void GLRenderer::init() {
	if (std::find(s_instances.begin(), s_instances.end(), this) == s_instances.end()) {
		s_instances.push_back(this);
	}
	if (initialized) {
		return;
	}

	if (!gladLoadGLLoader((GLADloadproc)rmeGetGLProc)) {
		wxLogError("GLRenderer::init — gladLoadGLLoader failed");
		return;
	}

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertSrc, nullptr);
	glCompileShader(vs);
	{
		GLint ok = 0;
		glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			std::array<char, 512> log {};
			glGetShaderInfoLog(vs, log.size(), nullptr, log.data());
			wxLogError("GLRenderer::init — vertex shader compile error: %s", log.data());
			glDeleteShader(vs);
			return;
		}
	}

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragSrc, nullptr);
	glCompileShader(fs);
	{
		GLint ok = 0;
		glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			std::array<char, 512> log {};
			glGetShaderInfoLog(fs, log.size(), nullptr, log.data());
			wxLogError("GLRenderer::init — fragment shader compile error: %s", log.data());
			glDeleteShader(vs);
			glDeleteShader(fs);
			return;
		}
	}

	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	{
		GLint ok = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok) {
			std::array<char, 512> log {};
			glGetProgramInfoLog(program, log.size(), nullptr, log.data());
			wxLogError("GLRenderer::init — program link error: %s", log.data());
			glDeleteProgram(program);
			program = 0;
			glDeleteShader(vs);
			glDeleteShader(fs);
			return;
		}
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	loc_projection = glGetUniformLocation(program, "uProjection");
	loc_useTexture = glGetUniformLocation(program, "uUseTexture");
	loc_texture = glGetUniformLocation(program, "uTexture");
	loc_stipple = glGetUniformLocation(program, "uStipple");

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Persistent ring-buffered, indexed stream for the quad batch path.
	glGenVertexArrays(1, &streamVAO);
	glGenBuffers(1, &streamVBO);
	glGenBuffers(1, &streamEBO);

	glBindVertexArray(streamVAO);
	glBindBuffer(GL_ARRAY_BUFFER, streamVBO);
	glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, streamEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, STREAM_EBO_CAPACITY * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));

	// Unbind the VAO first so the element-buffer binding stays recorded in streamVAO.
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	initialized = true;
}

void GLRenderer::shutdown() {
	current_texture = 0;
	m_programBound = false;
	std::erase(s_instances, this);
	if (!initialized) {
		return;
	}
	if (program) {
		glDeleteProgram(program);
		program = 0;
	}
	if (vbo) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if (vao) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
	if (streamVBO) {
		glDeleteBuffers(1, &streamVBO);
		streamVBO = 0;
	}
	if (streamEBO) {
		glDeleteBuffers(1, &streamEBO);
		streamEBO = 0;
	}
	if (streamVAO) {
		glDeleteVertexArrays(1, &streamVAO);
		streamVAO = 0;
	}
	vboOffset = 0;
	eboOffset = 0;
	initialized = false;
}

void GLRenderer::bindProgram() {
	if (m_programBound) {
		return;
	}
	glUseProgram(program);
	m_programBound = true;
}

void GLRenderer::bindState() {
	bindProgram();
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
}

void GLRenderer::unbindState() {
	if (!m_programBound) {
		return;
	}
	glBindVertexArray(0);
	glUseProgram(0);
	m_programBound = false;
}

void GLRenderer::endFrame() {
	flushBatch();
	unbindState();
	current_texture = 0;
}

void GLRenderer::flushAndUnbind() {
	flushBatch();
	unbindState();
}

void GLRenderer::setOrtho(float left, float right, float bottom, float top) {
	std::array<float, 16> m {};
	m[0] = 2.0f / (right - left);
	m[5] = 2.0f / (top - bottom);
	m[10] = -1.0f;
	m[12] = -(right + left) / (right - left);
	m[13] = -(top + bottom) / (top - bottom);
	m[15] = 1.0f;

	bindState();
	glUniformMatrix4fv(loc_projection, 1, GL_FALSE, m.data());
}

void GLRenderer::ensureQuadIndices(size_t quadCount) {
	size_t haveQuads = indexScratch.size() / 6;
	if (haveQuads >= quadCount) {
		return;
	}
	indexScratch.reserve(quadCount * 6);
	for (size_t q = haveQuads; q < quadCount; ++q) {
		GLuint base = static_cast<GLuint>(q * 4);
		indexScratch.push_back(base + 0);
		indexScratch.push_back(base + 1);
		indexScratch.push_back(base + 2);
		indexScratch.push_back(base + 0);
		indexScratch.push_back(base + 2);
		indexScratch.push_back(base + 3);
	}
}

void GLRenderer::flushBatch() {
	if (batch.empty() || !initialized) {
		return;
	}

	const size_t quadCount = batch.size() / 4;
	if (quadCount == 0) {
		batch.clear();
		return;
	}

	bindProgram();
	glBindVertexArray(streamVAO);
	glBindBuffer(GL_ARRAY_BUFFER, streamVBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, streamEBO);

	if (current_texture != 0) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, current_texture);
		glUniform1i(loc_useTexture, 1);
		glUniform1i(loc_texture, 0);
	} else {
		glUniform1i(loc_useTexture, 0);
	}

	// A single flush can exceed the ring capacity (RME batches all same-texture quads
	// eagerly). Split it into capacity-sized chunks instead of dropping vertices.
	constexpr size_t QUADS_PER_CHUNK = STREAM_VBO_CAPACITY / 4; // also == STREAM_EBO_CAPACITY / 6
	size_t quadStart = 0;
	while (quadStart < quadCount) {
		const size_t chunkQuads = std::min(QUADS_PER_CHUNK, quadCount - quadStart);
		const size_t chunkVerts = chunkQuads * 4;
		const size_t chunkIdx = chunkQuads * 6;
		const size_t vtxBytes = chunkVerts * sizeof(Vertex);
		const size_t idxBytes = chunkIdx * sizeof(GLuint);

		ensureQuadIndices(chunkQuads);

		if (vboOffset + chunkVerts > STREAM_VBO_CAPACITY || eboOffset + chunkIdx > STREAM_EBO_CAPACITY) {
			// Orphan both buffers and restart the ring from the beginning.
			glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, STREAM_EBO_CAPACITY * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
			vboOffset = 0;
			eboOffset = 0;
		}

		void* vboPtr = glMapBufferRange(GL_ARRAY_BUFFER, vboOffset * sizeof(Vertex), vtxBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		if (!vboPtr) {
			break;
		}
		std::memcpy(vboPtr, batch.data() + quadStart * 4, vtxBytes);
		glUnmapBuffer(GL_ARRAY_BUFFER);

		void* eboPtr = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, eboOffset * sizeof(GLuint), idxBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		if (!eboPtr) {
			break;
		}
		std::memcpy(eboPtr, indexScratch.data(), idxBytes);
		glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

		glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(chunkIdx), GL_UNSIGNED_INT, (void*)(eboOffset * sizeof(GLuint)), static_cast<GLint>(vboOffset));

		vboOffset += chunkVerts;
		eboOffset += chunkIdx;
		quadStart += chunkQuads;
	}

	batch.clear();
}

void GLRenderer::pushQuad(const Vertex &v0, const Vertex &v1, const Vertex &v2, const Vertex &v3) {
	batch.push_back(v0);
	batch.push_back(v1);
	batch.push_back(v2);
	batch.push_back(v3);
}

void GLRenderer::drawTexturedQuad(float x, float y, float w, float h, GLuint textureId, const GLColor &color, float u0, float v0_, float u1, float v1_) {
	if (current_texture != textureId && !batch.empty()) {
		flushBatch();
	}
	current_texture = textureId;

	Vertex v0 = { x, y, u0, v0_, color.r, color.g, color.b, color.a };
	Vertex v1 = { x + w, y, u1, v0_, color.r, color.g, color.b, color.a };
	Vertex v2 = { x + w, y + h, u1, v1_, color.r, color.g, color.b, color.a };
	Vertex v3 = { x, y + h, u0, v1_, color.r, color.g, color.b, color.a };

	pushQuad(v0, v1, v2, v3);
}

void GLRenderer::drawColoredQuad(float x, float y, float w, float h, const GLColor &color) {
	if (current_texture != 0 && !batch.empty()) {
		flushBatch();
	}
	current_texture = 0;

	Vertex v0 = { x, y, 0, 0, color.r, color.g, color.b, color.a };
	Vertex v1 = { x + w, y, 0, 0, color.r, color.g, color.b, color.a };
	Vertex v2 = { x + w, y + h, 0, 0, color.r, color.g, color.b, color.a };
	Vertex v3 = { x, y + h, 0, 0, color.r, color.g, color.b, color.a };

	pushQuad(v0, v1, v2, v3);
}

void GLRenderer::drawThickLineSegment(float x1, float y1, float x2, float y2, float width, const GLColor &color) {
	float dx = x2 - x1;
	float dy = y2 - y1;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-6f) {
		return;
	}
	float nx = (-dy / len) * (width * 0.5f);
	float ny = (dx / len) * (width * 0.5f);

	if (current_texture != 0 && !batch.empty()) {
		flushBatch();
	}
	current_texture = 0;

	Vertex v0 = { x1 + nx, y1 + ny, 0, 0, color.r, color.g, color.b, color.a };
	Vertex v1 = { x1 - nx, y1 - ny, 0, 0, color.r, color.g, color.b, color.a };
	Vertex v2 = { x2 - nx, y2 - ny, 0, 0, color.r, color.g, color.b, color.a };
	Vertex v3 = { x2 + nx, y2 + ny, 0, 0, color.r, color.g, color.b, color.a };

	pushQuad(v0, v1, v2, v3);
}

void GLRenderer::drawRect(float x, float y, float w, float h, const GLColor &color, float lineWidth) {
	drawThickLineSegment(x, y, x + w, y, lineWidth, color);
	drawThickLineSegment(x + w, y, x + w, y + h, lineWidth, color);
	drawThickLineSegment(x + w, y + h, x, y + h, lineWidth, color);
	drawThickLineSegment(x, y + h, x, y, lineWidth, color);
}

void GLRenderer::drawRoundedRect(float x, float y, float w, float h, float radius, const GLColor &fill) {
	const int segments = 8;
	flushBatch();

	std::vector<Vertex> verts;
	// center vertex for fan
	float cx = x + w * 0.5f;
	float cy = y + h * 0.5f;
	Vertex center = { cx, cy, 0, 0, fill.r, fill.g, fill.b, fill.a };

	// corners: top-left, top-right, bottom-right, bottom-left
	std::array<std::array<float, 2>, 4> corners = { {
		{ x + radius, y + radius },
		{ x + w - radius, y + radius },
		{ x + w - radius, y + h - radius },
		{ x + radius, y + h - radius },
	} };

	constexpr float pi = std::numbers::pi_v<float>;
	std::array<float, 4> startAngle = { pi, 1.5f * pi, 0.0f, 0.5f * pi };

	// build perimeter vertices
	std::vector<Vertex> perimeter;
	for (int c = 0; c < 4; ++c) {
		for (int s = 0; s <= segments; ++s) {
			float angle = startAngle[c] + (s / static_cast<float>(segments)) * (pi * 0.5f);
			float px = corners[c][0] + cosf(angle) * radius;
			float py = corners[c][1] + sinf(angle) * radius;
			perimeter.push_back({ px, py, 0, 0, fill.r, fill.g, fill.b, fill.a });
		}
	}

	// triangle fan from center
	std::vector<Vertex> tris;
	for (size_t i = 0; i < perimeter.size(); ++i) {
		size_t next = (i + 1) % perimeter.size();
		tris.push_back(center);
		tris.push_back(perimeter[i]);
		tris.push_back(perimeter[next]);
	}

	bindState();
	glUniform1i(loc_useTexture, 0);
	glBufferData(GL_ARRAY_BUFFER, tris.size() * sizeof(Vertex), tris.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)tris.size());
}

void GLRenderer::drawRoundedRectOutline(float x, float y, float w, float h, float radius, const GLColor &color, float lineWidth) {
	const int segments = 8;

	std::array<std::array<float, 2>, 4> corners = { {
		{ x + radius, y + radius },
		{ x + w - radius, y + radius },
		{ x + w - radius, y + h - radius },
		{ x + radius, y + h - radius },
	} };

	constexpr float pi = std::numbers::pi_v<float>;
	std::array<float, 4> startAngle = { pi, 1.5f * pi, 0.0f, 0.5f * pi };

	std::vector<std::array<float, 2>> perimeter;
	for (int c = 0; c < 4; ++c) {
		for (int s = 0; s <= segments; ++s) {
			float angle = startAngle[c] + (s / static_cast<float>(segments)) * (pi * 0.5f);
			float px = corners[c][0] + cosf(angle) * radius;
			float py = corners[c][1] + sinf(angle) * radius;
			perimeter.push_back({ px, py });
		}
	}

	for (size_t i = 0; i < perimeter.size(); ++i) {
		size_t next = (i + 1) % perimeter.size();
		drawThickLineSegment(perimeter[i][0], perimeter[i][1], perimeter[next][0], perimeter[next][1], lineWidth, color);
	}
}

void GLRenderer::drawLine(float x1, float y1, float x2, float y2, const GLColor &color, float width) {
	drawThickLineSegment(x1, y1, x2, y2, width, color);
}

void GLRenderer::drawLines(const float* vertices, int pairCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width) {
	GLColor c = { r, g, b, a };
	for (int i = 0; i < pairCount; ++i) {
		float x1 = vertices[i * 4];
		float y1 = vertices[i * 4 + 1];
		float x2 = vertices[i * 4 + 2];
		float y2 = vertices[i * 4 + 3];
		drawThickLineSegment(x1, y1, x2, y2, width, c);
	}
}

void GLRenderer::drawStippledLines(const float* vertices, int pairCount, const GLColor &color, float width, int factor, uint16_t pattern) {
	for (int i = 0; i < pairCount; ++i) {
		float x1 = vertices[i * 4];
		float y1 = vertices[i * 4 + 1];
		float x2 = vertices[i * 4 + 2];
		float y2 = vertices[i * 4 + 3];

		float dx = x2 - x1;
		float dy = y2 - y1;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 1e-6f) {
			continue;
		}

		float dirX = dx / len;
		float dirY = dy / len;
		auto step = static_cast<float>(factor);
		int bit = 0;
		float pos = 0.0f;

		while (pos < len) {
			float segEnd = pos + step;
			if (segEnd > len) {
				segEnd = len;
			}

			if (pattern & (1 << (bit & 15))) {
				float sx = x1 + dirX * pos;
				float sy = y1 + dirY * pos;
				float ex = x1 + dirX * segEnd;
				float ey = y1 + dirY * segEnd;
				drawThickLineSegment(sx, sy, ex, ey, width, color);
			}

			pos = segEnd;
			bit++;
		}
	}
}

void GLRenderer::drawPolygon(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (vertexCount < 3) {
		return;
	}
	flushBatch();
	std::vector<Vertex> verts;
	for (int i = 0; i < vertexCount; ++i) {
		verts.push_back({ vertices[i * 2], vertices[i * 2 + 1], 0, 0, r, g, b, a });
	}
	bindState();
	glUniform1i(loc_useTexture, 0);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)verts.size());
}

void GLRenderer::drawTriangleFan(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (vertexCount < 3) {
		return;
	}
	flushBatch();
	std::vector<Vertex> verts;
	for (int i = 0; i < vertexCount; ++i) {
		verts.push_back({ vertices[i * 2], vertices[i * 2 + 1], 0, 0, r, g, b, a });
	}
	bindState();
	glUniform1i(loc_useTexture, 0);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)verts.size());
}

void GLRenderer::flush() {
	flushBatch();
}

void GLRenderer::invalidateTexture(GLuint id) {
	for (auto* inst : s_instances) {
		if (inst->current_texture == id) {
			inst->current_texture = 0;
		}
	}
}

void GLRenderer::ensureFBO(int w, int h) {
	if (w <= 0 || h <= 0) {
		return;
	}
	if (fboData.fbo != 0 && fboData.width == w && fboData.height == h) {
		return;
	}
	destroyFBO();

	glGenFramebuffers(1, &fboData.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fboData.fbo);

	glGenTextures(1, &fboData.texture);
	glBindTexture(GL_TEXTURE_2D, fboData.texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboData.texture, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glDeleteTextures(1, &fboData.texture);
		glDeleteFramebuffers(1, &fboData.fbo);
		fboData.fbo = 0;
		fboData.texture = 0;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	fboData.width = w;
	fboData.height = h;
}

void GLRenderer::destroyFBO() {
	if (fboData.texture != 0) {
		invalidateTexture(fboData.texture);
		glDeleteTextures(1, &fboData.texture);
		fboData.texture = 0;
	}
	if (fboData.fbo != 0) {
		glDeleteFramebuffers(1, &fboData.fbo);
		fboData.fbo = 0;
	}
	fboData.width = 0;
	fboData.height = 0;
}

void GLRenderer::beginFBO() {
	if (fboData.fbo != 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, fboData.fbo);
	}
}

void GLRenderer::endFBO() {
	if (fboData.fbo != 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

void GLRenderer::blitFBO(int w, int h) {
	if (fboData.fbo == 0 || w <= 0 || h <= 0) {
		return;
	}
	flush();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fboData.fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
