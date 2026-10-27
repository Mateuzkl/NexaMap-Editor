#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
	gtk_init(&argc, &argv);
	if (argc != 3) {
		return 2;
	}
	auto* window = gdk_x11_window_foreign_new_for_display(gdk_display_get_default(), std::strtoul(argv[1], nullptr, 10));
	if (!window) {
		return 1;
	}
	auto* pixels = gdk_pixbuf_get_from_window(window, 0, 0, gdk_window_get_width(window), gdk_window_get_height(window));
	if (!pixels) {
		g_object_unref(window);
		return 1;
	}
	GError* error = nullptr;
	const bool saved = gdk_pixbuf_save(pixels, argv[2], "png", &error, nullptr);
	if (error) {
		std::cerr << error->message << '\n';
		g_error_free(error);
	}
	g_object_unref(pixels);
	g_object_unref(window);
	return saved ? 0 : 1;
}
