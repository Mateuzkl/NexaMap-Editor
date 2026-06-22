#ifndef RME_GL_RENDERER_H_
#define RME_GL_RENDERER_H_

#include <string>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <array>

// Minimal GL type forward declarations — full GL comes from glad in gl_renderer.cpp
using GLuint = unsigned int;
using GLint = int;

struct GLColor {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

class GLRenderer {
public:
	void init();

	void drawTexturedQuad(float x, float y, float w, float h, GLuint textureId, const GLColor &color, float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f);
	void drawColoredQuad(float x, float y, float w, float h, const GLColor &color);

	void drawRect(float x, float y, float w, float h, const GLColor &color, float lineWidth = 1.0f);

	void drawLine(float x1, float y1, float x2, float y2, const GLColor &color, float width = 1.0f);
	void drawLines(const float* vertices, int pairCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width = 1.0f);
	void drawStippledLines(const float* vertices, int pairCount, const GLColor &color, float width = 1.0f, int factor = 2, uint16_t pattern = 0xAAAA);

	void drawPolygon(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
	void drawTriangleFan(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

	void setOrtho(float left, float right, float bottom, float top);

	void flush();
	void endFrame();
	void flushAndUnbind();
	static void invalidateTexture(GLuint id);

	void ensureFBO(int w, int h);
	void destroyFBO();
	void beginFBO();
	void endFBO();
	void blitFBO(int w, int h);
	bool hasFBO() const {
		return fboData.fbo != 0;
	}

private:
	static std::vector<GLRenderer*> s_instances;
	bool initialized = false;
	bool m_programBound = false;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint program = 0;
	GLint loc_projection = -1;
	GLint loc_useTexture = -1;
	GLint loc_texture = -1;
	GLint loc_stipple = -1;

	// Persistent ring-buffered, indexed quad stream (4 verts + 6 indices per quad).
	static constexpr size_t STREAM_VBO_CAPACITY = 64 * 1024; // vertices
	static constexpr size_t STREAM_EBO_CAPACITY = 96 * 1024; // indices
	GLuint streamVAO = 0;
	GLuint streamVBO = 0;
	GLuint streamEBO = 0;
	size_t vboOffset = 0; // in vertices
	size_t eboOffset = 0; // in indices
	std::vector<GLuint> indexScratch; // reusable local index pattern

	struct Vertex {
		float x;
		float y;
		float u;
		float v;
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	std::vector<Vertex> batch; // 4 vertices per quad
	GLuint current_texture = 0;

	struct FBOData {
		GLuint fbo = 0;
		GLuint texture = 0;
		int width = 0;
		int height = 0;
	};
	FBOData fboData;

	void flushBatch();
	void bindProgram();
	void bindState();
	void unbindState();
	void pushQuad(const Vertex &v0, const Vertex &v1, const Vertex &v2, const Vertex &v3);
	void ensureQuadIndices(size_t quadCount);
	void drawThickLineSegment(float x1, float y1, float x2, float y2, float width, const GLColor &color);
};

#endif
