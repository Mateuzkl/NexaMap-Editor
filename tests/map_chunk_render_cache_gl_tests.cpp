#include <glad/glad.h>
#include <wx/wx.h>
#include <wx/glcanvas.h>
#include "map_chunk_render_cache.h"
#include "map_chunk_revision.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

class CacheTestApp : public wxApp {
public:
	bool OnInit() override {
		return true;
	}
};
wxIMPLEMENT_APP_NO_MAIN(CacheTestApp);

namespace {
	void require(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}
	struct Frame {
		std::vector<uint8_t> pixels;
		GLRenderBatchStats stats;
		double submitMs = 0;
	};

	void run() {
		const int attributes[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, 0 };
		auto window = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Offscreen GL cache tests", wxDefaultPosition, wxSize(192, 192));
		auto* canvas = new wxGLCanvas(window.get(), wxID_ANY, attributes, wxDefaultPosition, wxSize(192, 192));
		wxGLContext context(canvas);
		require(context.IsOK() && canvas->SetCurrent(context), "OpenGL context creation");
		GLRenderer renderer;
		renderer.init();
		require(glGetString(GL_VERSION) != nullptr, "OpenGL driver unavailable");
		std::cout << "GL: " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';
		renderer.ensureFBO(192, 192);
		require(renderer.hasFBO(), "offscreen framebuffer");
		glDisable(GL_DITHER);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLuint texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		const uint8_t pixels[] = { 250, 30, 80, 255, 40, 230, 90, 128, 40, 60, 240, 255, 235, 210, 80, 255 };
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		AtlasPageEpochs epochs;
		auto token = epochs.replace(texture);
		const auto firstToken = token;
		epochs.clear();
		require(!epochs.contains(firstToken), "clear invalidates atlas token");
		token = epochs.replace(texture);
		require(token != firstToken && !epochs.contains(firstToken), "same GL name gets a new epoch");
		MapChunkGeometry geometry;
		for (size_t i = 0; i < 16; ++i) {
			geometry.grounds[i] = { -4, -6, 2, static_cast<uint32_t>(i), 1 };
		}
		geometry.quadCount = 16;
		bool remap = false, available = true;
		auto resolve = [&](const MapChunkGroundQuad& q) {
			const float start = ((q.imageIndex % 2 != 0) != remap) ? 0.5f : 0.0f;
			return ChunkAtlasSprite { token, start, 0, start + 0.5f, 1 };
		};
		auto retain = [&](AtlasPageToken page) { return available && epochs.contains(page); };
		MapChunkRenderCache cache(renderer, 2, 2560);
		const uint32_t key = MakeMapChunkKey(100, 100, 7);
		MapChunkRevisionTracker tracker;
		MapChunkRevision revision;
		tracker.mark(revision);
		auto capture = [&](bool cached, int camera = 0, int selected = -1, bool overlays = true) {
			Frame result;
			const auto start = std::chrono::steady_clock::now();
			renderer.beginFrame();
			renderer.beginFBO();
			glViewport(0, 0, 192, 192);
			glClearColor(0.08f, 0.12f, 0.2f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			renderer.setOrtho(0, 192, 192, 0);
			cache.beginPass(1, 10, cached);
			const auto* entry = cached ? cache.prepare(key, tracker.read(revision).content, geometry, resolve, retain) : nullptr;
			for (size_t i = 0; i < geometry.grounds.size(); ++i) {
				const auto& q = geometry.grounds[i];
				if (!q.itemId) {
					continue;
				}
				const int x = 16 + static_cast<int>(i / 4) * 32 + camera;
				const int y = 16 + static_cast<int>(i % 4) * 32;
				const GLColor color = { static_cast<uint8_t>(i == selected ? 120 : 210), 170, 235, 255 };
				if (!entry || static_cast<int>(i) == selected || !cache.draw(*entry, i, x, y, color)) {
					const auto uv = resolve(q);
					renderer.drawTexturedQuad(x + q.offsetX, y + q.offsetY, 32, 32, texture, color, uv.u0, uv.v0, uv.u1, uv.v1);
				}
				// Live alpha content interrupts static ranges at original tile slots.
				if (overlays && i % 3 == 0) {
					renderer.drawColoredQuad(x + 12, y + 14, 30, 30, { 50, 225, 190, 120 });
				}
			}
			if (overlays) {
				const float triangle[] = { 35, 28, 175, 130, 60, 175 };
				renderer.drawPolygon(triangle, 3, 120, 50, 200, 95);
			}
			renderer.endFrame();
			result.submitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			result.stats = renderer.getFrameStats();
			result.pixels.resize(192 * 192 * 4);
			glReadPixels(0, 0, 192, 192, GL_RGBA, GL_UNSIGNED_BYTE, result.pixels.data());
			renderer.endFBO();
			require(glGetError() == GL_NO_ERROR, "OpenGL error during render/readback");
			return result;
		};
		const auto baseline = capture(false);
		const auto cold = capture(true);
		require(baseline.pixels == cold.pixels, "OFF/ON painter order, alpha and tint equivalence");
		require(cache.getStats().rebuilt == 1 && cache.getStats().uploadedBytes == 1280, "cold GPU upload");
		const auto warm = capture(true);
		require(warm.pixels == baseline.pixels && cache.getStats().hits == 1 && cache.getStats().uploadedBytes == 0, "warm GPU reuse");
		require(warm.stats.streamBytes < baseline.stats.streamBytes, "warm grounds bypass streaming uploads");
		capture(true, 11);
		require(cache.getStats().rebuilt == 0 && cache.getStats().hits == 1, "pan does not upload geometry");
		tracker.mark(revision, MapChunkChange::Presentation);
		const auto selected = capture(true, 11, 3);
		require(cache.getStats().uploadedBytes == 0, "selection stays live without GPU rebuild");
		require(selected.pixels == capture(false, 11, 3).pixels, "selection OFF/ON equivalence");
		capture(true);
		const auto bufferBeforeEdit = cache.prepare(key, revision.content, geometry, resolve, retain)->buffer;
		geometry.grounds[1].itemId = 0;
		--geometry.quadCount;
		tracker.mark(revision);
		const auto deleted = capture(true);
		require(cache.getStats().rebuilt == 1 && cache.prepare(key, revision.content, geometry, resolve, retain)->buffer == bufferBeforeEdit, "delete reuses existing VBO capacity");
		require(deleted.pixels == capture(false).pixels, "deleted ground does not persist");
		geometry.grounds[1].itemId = 1;
		++geometry.quadCount;
		tracker.mark(revision);
		require(capture(true).pixels == baseline.pixels, "recreate/undo restores pixels");
		remap = true;
		token = epochs.replace(texture);
		const auto remapped = capture(true);
		require(cache.getStats().rebuilt == 1, "atlas recycle with unchanged GLuint rebuilds UVs");
		require(remapped.pixels == capture(false).pixels, "recycled atlas OFF/ON equivalence");
		capture(true);
		available = false;
		capture(true);
		require(cache.getStats().fallback == 1 && cache.getStats().replayedQuads == 0, "missing page never replays stale UVs");
		available = true;
		cache.beginPass(2, 10, true);
		require(cache.getChunkCount() == 0, "map identity releases resident VBOs");
		cache.prepare(key, 1, geometry, resolve, retain);
		cache.beginPass(2, 11, true);
		require(cache.getMemoryBytes() == 0, "resource switch releases VBOs");
		cache.prepare(key, 1, geometry, resolve, retain);
		cache.prepare(MakeMapChunkKey(104, 100, 7), 1, geometry, resolve, retain);
		require(!cache.prepare(MakeMapChunkKey(108, 100, 7), 1, geometry, resolve, retain), "current-frame geometry is pinned under budget pressure");
		cache.beginPass(2, 11, true);
		require(cache.prepare(MakeMapChunkKey(108, 100, 7), 1, geometry, resolve, retain) != nullptr, "LRU evicts previous-frame geometry");
		require(cache.getStats().evicted == 1 && cache.getMemoryBytes() <= 2560, "GPU budget accounting");
		cache.clear();
		require(cache.getMemoryBytes() == 0 && cache.getChunkCount() == 0, "release clears resident buffers");
		std::cout << "Pixel equivalence: exact; warm stream bytes " << baseline.stats.streamBytes << " -> " << warm.stats.streamBytes << "; draw calls " << baseline.stats.drawCalls << " -> " << warm.stats.drawCalls << '\n';
		for (bool enabled : { false, true, false, true }) {
			std::vector<double> times;
			Frame last;
			for (int i = 0; i < 140; ++i) {
				last = capture(enabled);
				if (i >= 20) {
					times.push_back(last.submitMs);
				}
			}
			std::sort(times.begin(), times.end());
			std::cout << "Synthetic 16-ground mixed scene " << (enabled ? "ON" : "OFF") << " n=" << times.size() << " submit median_ms=" << times[times.size() / 2] << " p95_ms=" << times[times.size() * 95 / 100] << " draws=" << last.stats.drawCalls << " binds=" << last.stats.textureBindings << " stream_bytes=" << last.stats.streamBytes << '\n';
		}
		cache.clear();
		glDeleteTextures(1, &texture);
		std::cout << "Persistent GPU cache integration tests passed\n";
	}
}

int main(int argc, char** argv) {
	if (!wxEntryStart(argc, argv)) {
		return 77;
	}
	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return 77;
	}
	int result = 0;
	try {
		run();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	return result;
}
