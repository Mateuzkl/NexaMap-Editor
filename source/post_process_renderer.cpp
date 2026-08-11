#include <glad/glad.h>

#include <wx/log.h>

#include "post_process_renderer.h"

#include <algorithm>
#include <array>

namespace {

	const char* const vertexShaderSource = R"(
#version 330
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
	vUV = aUV;
}
)";

	const char* const fragmentShaderSource = R"(
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

	GLuint compileShader(GLenum type, const char* source) {
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
		wxLogError("Post-process shader compile error: %s", log.data());
		glDeleteShader(shader);
		return 0;
	}

} // namespace

bool PostProcessRenderer::ensureResources() {
	if (program != 0) {
		return true;
	}
	if (failureLogged) {
		return false;
	}

	const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
	const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
	if (vertexShader == 0 || fragmentShader == 0) {
		if (vertexShader != 0) {
			glDeleteShader(vertexShader);
		}
		if (fragmentShader != 0) {
			glDeleteShader(fragmentShader);
		}
		failureLogged = true;
		return false;
	}

	program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	GLint linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE) {
		std::array<char, 1024> log {};
		glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
		wxLogError("Post-process program link error: %s", log.data());
		glDeleteProgram(program);
		program = 0;
		failureLogged = true;
		return false;
	}

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	if (vao == 0 || vbo == 0) {
		wxLogError("Post-process buffer allocation failed; using the normal scene blit.");
		release();
		failureLogged = true;
		return false;
	}

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	sceneLocation = glGetUniformLocation(program, "uScene");
	effectLocation = glGetUniformLocation(program, "uEffect");
	texelSizeLocation = glGetUniformLocation(program, "uTexelSize");
	return true;
}

bool PostProcessRenderer::draw(
	unsigned int texture,
	int textureWidth,
	int textureHeight,
	int srcX0,
	int srcY0,
	int srcX1,
	int srcY1,
	int dstX0,
	int dstY0,
	int dstX1,
	int dstY1,
	unsigned int filter,
	int effect
) {
	if (texture == 0 || effect <= 0 || !ensureResources()) {
		return false;
	}

	const float screenWidth = static_cast<float>(std::max(1, textureWidth));
	const float screenHeight = static_cast<float>(std::max(1, textureHeight));
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
	glUseProgram(program);
	glUniform1i(sceneLocation, 0);
	glUniform1i(effectLocation, effect);
	glUniform2f(texelSizeLocation, 1.0f / screenWidth, 1.0f / screenHeight);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	if (blendEnabled == GL_TRUE) {
		glEnable(GL_BLEND);
	}
	return true;
}

void PostProcessRenderer::release() {
	if (vao != 0) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
	if (vbo != 0) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if (program != 0) {
		glDeleteProgram(program);
		program = 0;
	}
}
