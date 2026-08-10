#ifndef RME_POST_PROCESS_RENDERER_H_
#define RME_POST_PROCESS_RENDERER_H_

class PostProcessRenderer {
public:
	bool draw(
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
	);
	void release();

private:
	bool ensureResources();

	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int program = 0;
	int sceneLocation = -1;
	int effectLocation = -1;
	int texelSizeLocation = -1;
	bool failureLogged = false;
};

#endif
