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

#include <glad/glad.h>

#include "main.h"
#include "gl_renderer.h"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
static void* rmeGetGLProc(const char* name) {
	auto p = reinterpret_cast<void*>(wglGetProcAddress(name));
	if (p == nullptr || p == reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1)) || p == reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2)) || p == reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x3)) || p == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1))) {
		static HMODULE gl = LoadLibraryA("opengl32.dll");
		p = reinterpret_cast<void*>(GetProcAddress(gl, name));
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
	void* p = reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
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

GLRenderer::~GLRenderer() {
	destroyFBO();
	destroyPostProcess();

	if (m_programBound) {
		glUseProgram(0);
		m_programBound = false;
	}
	if (vao != 0) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
	if (vbo != 0) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if (streamVAO != 0) {
		glDeleteVertexArrays(1, &streamVAO);
		streamVAO = 0;
	}
	if (streamVBO != 0) {
		glDeleteBuffers(1, &streamVBO);
		streamVBO = 0;
	}
	if (streamEBO != 0) {
		glDeleteBuffers(1, &streamEBO);
		streamEBO = 0;
	}
	if (program != 0) {
		glDeleteProgram(program);
		program = 0;
	}
	initialized = false;
	current_texture = 0;
	batch.clear();
	indexScratch.clear();

	s_instances.erase(std::remove(s_instances.begin(), s_instances.end(), this), s_instances.end());
}

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

static const char* const postProcessVertSrc = R"(
#version 330
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
	vUV = aUV;
}
)";

static const char* const postProcessFragSrc = R"(
#version 330
in vec2 vUV;
uniform sampler2D uScene;
uniform vec2 uTexelSize;
uniform int uEffect;
out vec4 FragColor;
void main() {
	vec4 center = texture(uScene, vUV);
	if (uEffect == 1) {
		vec3 sharpened = center.rgb * 5.0
			- texture(uScene, vUV + vec2(uTexelSize.x, 0.0)).rgb
			- texture(uScene, vUV - vec2(uTexelSize.x, 0.0)).rgb
			- texture(uScene, vUV + vec2(0.0, uTexelSize.y)).rgb
			- texture(uScene, vUV - vec2(0.0, uTexelSize.y)).rgb;
		center.rgb = mix(center.rgb, clamp(sharpened, 0.0, 1.0), 0.35);
	} else if (uEffect == 2) {
		float scanline = 0.88 + 0.12 * sin(gl_FragCoord.y * 3.14159265);
		center.rgb = pow(center.rgb * scanline, vec3(0.95));
	}
	FragColor = center;
}
)";

void GLRenderer::init() {
	if (std::find(s_instances.begin(), s_instances.end(), this) == s_instances.end()) {
		s_instances.push_back(this);
	}
	if (initialized) {
		return;
	}

	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(rmeGetGLProc))) {
		wxLogError("GLRenderer::init — gladLoadGLLoader failed");
		return;
	}

	const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
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

	const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
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
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Ring-buffered vertices and immutable indices for the quad batch path.
	glGenVertexArrays(1, &streamVAO);
	glGenBuffers(1, &streamVBO);
	glGenBuffers(1, &streamEBO);

	glBindVertexArray(streamVAO);
	glBindBuffer(GL_ARRAY_BUFFER, streamVBO);
	glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, streamEBO);
	ensureQuadIndices(STREAM_EBO_CAPACITY / 6);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexScratch.size() * sizeof(GLuint), indexScratch.data(), GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

	// Unbind the VAO first so the element-buffer binding stays recorded in streamVAO.
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	batch.reserve(STREAM_VBO_CAPACITY);
	indexScratch.clear();
	indexScratch.shrink_to_fit();

	initialized = true;
}

void GLRenderer::beginFrame() noexcept {
	frameStats = {};
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
	const size_t haveQuads = indexScratch.size() / 6;
	if (haveQuads >= quadCount) {
		return;
	}
	indexScratch.reserve(quadCount * 6);
	for (size_t q = haveQuads; q < quadCount; ++q) {
		const auto base = static_cast<GLuint>(q * 4);
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
		++frameStats.textureBindings;
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

		if (vboOffset + chunkVerts > STREAM_VBO_CAPACITY) {
			// Orphan the dynamic vertex stream and restart the ring. Quad indices are static.
			glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
			vboOffset = 0;
			++frameStats.bufferOrphans;
		}

		void* vboPtr = glMapBufferRange(GL_ARRAY_BUFFER, vboOffset * sizeof(Vertex), vtxBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		if (!vboPtr) {
			if (!mappingFallbackLogged) {
				wxLogWarning("GLRenderer::flushBatch - stream mapping failed; using glBufferSubData fallback.");
				mappingFallbackLogged = true;
			}
			glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
			vboOffset = 0;
			glBufferSubData(GL_ARRAY_BUFFER, 0, vtxBytes, batch.data() + quadStart * 4);
			++frameStats.bufferOrphans;
			++frameStats.mappingFallbacks;
		} else {
			std::memcpy(vboPtr, batch.data() + quadStart * 4, vtxBytes);
			glUnmapBuffer(GL_ARRAY_BUFFER);
		}

		glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(chunkIdx), GL_UNSIGNED_INT, nullptr, static_cast<GLint>(vboOffset));
		++frameStats.drawCalls;
		frameStats.streamBytes += vtxBytes;

		vboOffset += chunkVerts;
		quadStart += chunkQuads;
	}

	batch.clear();
}

void GLRenderer::pushQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3) {
	if (batch.size() + 4 > STREAM_VBO_CAPACITY) {
		flushBatch();
	}
	batch.push_back(v0);
	batch.push_back(v1);
	batch.push_back(v2);
	batch.push_back(v3);
	++frameStats.quads;
}

void GLRenderer::drawTexturedQuad(float x, float y, float w, float h, GLuint textureId, const GLColor& color, float u0, float v0_, float u1, float v1_) {
	if (current_texture != textureId && !batch.empty()) {
		flushBatch();
	}
	current_texture = textureId;

	const Vertex v0 = { x, y, u0, v0_, color.r, color.g, color.b, color.a };
	const Vertex v1 = { x + w, y, u1, v0_, color.r, color.g, color.b, color.a };
	const Vertex v2 = { x + w, y + h, u1, v1_, color.r, color.g, color.b, color.a };
	const Vertex v3 = { x, y + h, u0, v1_, color.r, color.g, color.b, color.a };

	pushQuad(v0, v1, v2, v3);
}

void GLRenderer::drawColoredQuad(float x, float y, float w, float h, const GLColor& color) {
	if (current_texture != 0 && !batch.empty()) {
		flushBatch();
	}
	current_texture = 0;

	const Vertex v0 = { x, y, 0, 0, color.r, color.g, color.b, color.a };
	const Vertex v1 = { x + w, y, 0, 0, color.r, color.g, color.b, color.a };
	const Vertex v2 = { x + w, y + h, 0, 0, color.r, color.g, color.b, color.a };
	const Vertex v3 = { x, y + h, 0, 0, color.r, color.g, color.b, color.a };

	pushQuad(v0, v1, v2, v3);
}

void GLRenderer::drawThickLineSegment(float x1, float y1, float x2, float y2, float width, const GLColor& color) {
	const float dx = x2 - x1;
	const float dy = y2 - y1;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-6f) {
		return;
	}
	const float nx = (-dy / len) * (width * 0.5f);
	const float ny = (dx / len) * (width * 0.5f);

	if (current_texture != 0 && !batch.empty()) {
		flushBatch();
	}
	current_texture = 0;

	const Vertex v0 = { x1 + nx, y1 + ny, 0, 0, color.r, color.g, color.b, color.a };
	const Vertex v1 = { x1 - nx, y1 - ny, 0, 0, color.r, color.g, color.b, color.a };
	const Vertex v2 = { x2 - nx, y2 - ny, 0, 0, color.r, color.g, color.b, color.a };
	const Vertex v3 = { x2 + nx, y2 + ny, 0, 0, color.r, color.g, color.b, color.a };

	pushQuad(v0, v1, v2, v3);
}

void GLRenderer::drawRect(float x, float y, float w, float h, const GLColor& color, float lineWidth) {
	drawThickLineSegment(x, y, x + w, y, lineWidth, color);
	drawThickLineSegment(x + w, y, x + w, y + h, lineWidth, color);
	drawThickLineSegment(x + w, y + h, x, y + h, lineWidth, color);
	drawThickLineSegment(x, y + h, x, y, lineWidth, color);
}

void GLRenderer::drawLine(float x1, float y1, float x2, float y2, const GLColor& color, float width) {
	drawThickLineSegment(x1, y1, x2, y2, width, color);
}

void GLRenderer::drawLines(const float* vertices, int pairCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width) {
	const GLColor c = { r, g, b, a };
	for (int i = 0; i < pairCount; ++i) {
		const float x1 = vertices[i * 4];
		const float y1 = vertices[i * 4 + 1];
		const float x2 = vertices[i * 4 + 2];
		const float y2 = vertices[i * 4 + 3];
		drawThickLineSegment(x1, y1, x2, y2, width, c);
	}
}

void GLRenderer::drawStippledLines(const float* vertices, int pairCount, const GLColor& color, float width, int factor, uint16_t pattern) {
	for (int i = 0; i < pairCount; ++i) {
		const float x1 = vertices[i * 4];
		const float y1 = vertices[i * 4 + 1];
		const float x2 = vertices[i * 4 + 2];
		const float y2 = vertices[i * 4 + 3];

		const float dx = x2 - x1;
		const float dy = y2 - y1;
		const float len = sqrtf(dx * dx + dy * dy);
		if (len < 1e-6f) {
			continue;
		}

		const float dirX = dx / len;
		const float dirY = dy / len;
		auto step = static_cast<float>(factor);
		int bit = 0;
		float pos = 0.0f;

		while (pos < len) {
			float segEnd = pos + step;
			if (segEnd > len) {
				segEnd = len;
			}

			if (pattern & (1 << (bit & 15))) {
				const float sx = x1 + dirX * pos;
				const float sy = y1 + dirY * pos;
				const float ex = x1 + dirX * segEnd;
				const float ey = y1 + dirY * segEnd;
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
	verts.reserve(vertexCount);
	for (int i = 0; i < vertexCount; ++i) {
		verts.push_back({ vertices[i * 2], vertices[i * 2 + 1], 0, 0, r, g, b, a });
	}
	bindState();
	glUniform1i(loc_useTexture, 0);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(verts.size()));
	++frameStats.drawCalls;
}

void GLRenderer::drawTriangleFan(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (vertexCount < 3) {
		return;
	}
	flushBatch();
	std::vector<Vertex> verts;
	verts.reserve(vertexCount);
	for (int i = 0; i < vertexCount; ++i) {
		verts.push_back({ vertices[i * 2], vertices[i * 2 + 1], 0, 0, r, g, b, a });
	}
	bindState();
	glUniform1i(loc_useTexture, 0);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(verts.size()));
	++frameStats.drawCalls;
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboData.texture, 0);

	const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
		if (!fbo_failure_logged) {
			wxLogError(
				"GLRenderer::ensureFBO - incomplete framebuffer (status 0x%X, size %dx%d); scene cache disabled.",
				static_cast<unsigned int>(framebufferStatus),
				w,
				h
			);
			fbo_failure_logged = true;
		}
		glDeleteTextures(1, &fboData.texture);
		glDeleteFramebuffers(1, &fboData.fbo);
		fboData.fbo = 0;
		fboData.texture = 0;
	} else {
		fbo_failure_logged = false;
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

void GLRenderer::blitFBO(
	int srcX0,
	int srcY0,
	int srcX1,
	int srcY1,
	int dstX0,
	int dstY0,
	int dstX1,
	int dstY1,
	unsigned int filter,
	int postProcessEffect
) {
	if (fboData.fbo == 0 || srcX0 == srcX1 || srcY0 == srcY1 || dstX0 == dstX1 || dstY0 == dstY1) {
		return;
	}
	if (postProcessEffect > 0 && ensurePostProcess()) {
		drawPostProcessedFBO(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, filter, postProcessEffect);
		return;
	}
	flush();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fboData.fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, filter);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GLRenderer::ensurePostProcess() {
	if (postProcessProgram != 0) {
		return true;
	}
	if (post_process_failure_logged) {
		return false;
	}

	auto compileShader = [](GLenum type, const char* source) -> GLuint {
		const GLuint shader = glCreateShader(type);
		glShaderSource(shader, 1, &source, nullptr);
		glCompileShader(shader);
		GLint compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled == GL_TRUE) {
			return shader;
		}
		std::array<char, 1024> log {};
		glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
		wxLogError("GLRenderer post-process shader compile error: %s", log.data());
		glDeleteShader(shader);
		return 0;
	};

	const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, postProcessVertSrc);
	const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, postProcessFragSrc);
	if (vertexShader == 0 || fragmentShader == 0) {
		if (vertexShader != 0) {
			glDeleteShader(vertexShader);
		}
		if (fragmentShader != 0) {
			glDeleteShader(fragmentShader);
		}
		post_process_failure_logged = true;
		return false;
	}

	postProcessProgram = glCreateProgram();
	glAttachShader(postProcessProgram, vertexShader);
	glAttachShader(postProcessProgram, fragmentShader);
	glLinkProgram(postProcessProgram);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	GLint linked = GL_FALSE;
	glGetProgramiv(postProcessProgram, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE) {
		std::array<char, 1024> log {};
		glGetProgramInfoLog(postProcessProgram, static_cast<GLsizei>(log.size()), nullptr, log.data());
		wxLogError("GLRenderer post-process program link error: %s", log.data());
		glDeleteProgram(postProcessProgram);
		postProcessProgram = 0;
		post_process_failure_logged = true;
		return false;
	}

	glGenVertexArrays(1, &postProcessVAO);
	glGenBuffers(1, &postProcessVBO);
	if (postProcessVAO == 0 || postProcessVBO == 0) {
		wxLogError("GLRenderer post-process buffer allocation failed; using the normal scene blit.");
		destroyPostProcess();
		post_process_failure_logged = true;
		return false;
	}
	glBindVertexArray(postProcessVAO);
	glBindBuffer(GL_ARRAY_BUFFER, postProcessVBO);
	glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	postProcessEffectLocation = glGetUniformLocation(postProcessProgram, "uEffect");
	postProcessTexelSizeLocation = glGetUniformLocation(postProcessProgram, "uTexelSize");
	return true;
}

void GLRenderer::destroyPostProcess() {
	if (postProcessVAO != 0) {
		glDeleteVertexArrays(1, &postProcessVAO);
		postProcessVAO = 0;
	}
	if (postProcessVBO != 0) {
		glDeleteBuffers(1, &postProcessVBO);
		postProcessVBO = 0;
	}
	if (postProcessProgram != 0) {
		glDeleteProgram(postProcessProgram);
		postProcessProgram = 0;
	}
}

void GLRenderer::drawPostProcessedFBO(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int filter, int effect) {
	flushAndUnbind();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	const float screenWidth = static_cast<float>(std::max(1, fboData.width));
	const float screenHeight = static_cast<float>(std::max(1, fboData.height));
	const float left = static_cast<float>(dstX0) / screenWidth * 2.0f - 1.0f;
	const float right = static_cast<float>(dstX1) / screenWidth * 2.0f - 1.0f;
	const float bottom = static_cast<float>(dstY0) / screenHeight * 2.0f - 1.0f;
	const float top = static_cast<float>(dstY1) / screenHeight * 2.0f - 1.0f;
	const float u0 = static_cast<float>(srcX0) / screenWidth;
	const float u1 = static_cast<float>(srcX1) / screenWidth;
	const float v0 = static_cast<float>(srcY0) / screenHeight;
	const float v1 = static_cast<float>(srcY1) / screenHeight;
	const float vertices[] = {
		left,
		bottom,
		u0,
		v0,
		right,
		bottom,
		u1,
		v0,
		right,
		top,
		u1,
		v1,
		left,
		top,
		u0,
		v1,
	};

	const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
	glDisable(GL_BLEND);
	glUseProgram(postProcessProgram);
	glUniform1i(glGetUniformLocation(postProcessProgram, "uScene"), 0);
	glUniform1i(postProcessEffectLocation, effect);
	glUniform2f(postProcessTexelSizeLocation, 1.0f / screenWidth, 1.0f / screenHeight);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, fboData.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
	glBindVertexArray(postProcessVAO);
	glBindBuffer(GL_ARRAY_BUFFER, postProcessVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	++frameStats.drawCalls;
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	if (blendEnabled == GL_TRUE) {
		glEnable(GL_BLEND);
	}
}
