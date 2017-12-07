#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <gtk/gtk.h>
#include <iostream>

class Application
{
private:
	std::function<void()> _activate = [](){};

	static void _static_activate(void* a, void* b)
	{
		reinterpret_cast<Application*>(b)->_activate();
	}

public:
	GtkApplication* handle;

	Application(const char* id, int flags)
	{
		handle = gtk_application_new(id, G_APPLICATION_FLAGS_NONE);
	}
	~Application()
	{
		g_object_unref(handle);
	}

	template<class F> void onActivate(const F& f)
	{
		_activate = f;
		g_signal_connect(handle, "activate", G_CALLBACK(_static_activate), this);
	}

	void quit()
	{
		g_application_quit(G_APPLICATION(handle));
	}

	int run(int argc, char** argv)
	{
		return g_application_run(G_APPLICATION(handle), argc, argv);
	}
};

class Widget
{
public:
	GtkWidget* handle = 0;
	std::vector<Widget*> widgets;
};

class Window : public Widget
{
public:
	Window(Application* app)
	{
		handle = gtk_application_window_new(app->handle);
	}
	~Window()
	{
		for(auto w : widgets)
			delete w;
	}

	template <class W> W* add(W* w)
	{
		widgets.push_back(w);
		gtk_container_add(GTK_CONTAINER(handle), w->handle);
		return w;
	}

	void setDefaultSize(int w, int h) { gtk_window_set_default_size(GTK_WINDOW(handle), w, h); }

	void setIcon(const char* path)
	{
		auto icon = gdk_pixbuf_new_from_file(path, 0);
		gtk_window_set_icon(GTK_WINDOW(handle), icon);
		g_object_unref(G_OBJECT(icon));
	}

	void setTitle(const char* title) { gtk_window_set_title(GTK_WINDOW(handle), title); }

	void showAll() { gtk_widget_show_all(handle); }
};

class Button : public Widget
{
private:
	std::function<void()> _click = [](){};

	static void _static_click(GtkWidget* b, void* data)
	{
		reinterpret_cast<Button*>(data)->_click();
	}

public:
	Button(const char* text) { handle = gtk_button_new_with_label(text); }
	~Button() {}

	void onClick(std::function<void()> f)
	{
		_click = f;
		g_signal_connect(handle, "clicked", G_CALLBACK(_static_click), this);
	}
};

class Box : public Widget
{
public:
	static const GtkOrientation HORIZONTAL = GTK_ORIENTATION_HORIZONTAL;
	static const GtkOrientation VERTICAL = GTK_ORIENTATION_VERTICAL;
	std::vector<Widget*> children;

	Box(GtkOrientation orientation, int spacing = 0)
	{
		handle = gtk_box_new(orientation, spacing);
	}
	~Box() {}

	template <class W> W* insert(W* child, bool expand = false, bool fill = false, int padding = 0)
	{
		gtk_box_pack_start(GTK_BOX(handle), child->handle, expand, fill, padding);
		children.insert(children.begin(), child);
		return child;
	}

	template <class W> W* push(W* child, bool expand = false, bool fill = false, int padding = 0)
	{
		gtk_box_pack_end(GTK_BOX(handle), child->handle, expand, fill, padding);
		children.push_back(child);
		return child;
	}

	void setHomogeneous(bool h)
	{
		gtk_box_set_homogeneous(GTK_BOX(handle), h);
	}
};

class Label : public Widget
{
public:
	Label(const char* text)
	{
		handle = gtk_label_new(text);
	}
	~Label() {}
};

class TextView : public Widget
{
public:
	GtkTextBuffer* buffer;

	TextView(const char* text = "")
	{
		buffer = gtk_text_buffer_new(0);
		handle = gtk_text_view_new_with_buffer(buffer);
		setText(text);
	}
	~TextView()
	{
		g_object_unref(buffer);
	}

	void clear()
	{
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_end_iter(buffer, &end);
		gtk_text_buffer_delete(buffer, &start, &end);
	}

	TextView* setEditable(bool e)
	{
		gtk_text_view_set_editable(GTK_TEXT_VIEW(handle), e);
		return this;
	}

	void setMargin(int l, int t, int r, int b)
	{
		auto h = GTK_TEXT_VIEW(handle);
		gtk_text_view_set_border_window_size(h, GTK_TEXT_WINDOW_LEFT   , l);
		gtk_text_view_set_border_window_size(h, GTK_TEXT_WINDOW_TOP    , t);
		gtk_text_view_set_border_window_size(h, GTK_TEXT_WINDOW_RIGHT  , r);
		gtk_text_view_set_border_window_size(h, GTK_TEXT_WINDOW_BOTTOM , b);
	}

	TextView* setText(const char* t)
	{
		gtk_text_buffer_set_text(buffer, t, -1);
		return this;
	}

	TextView* showCursor(bool e)
	{
		gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(handle), e);
		return this;
	}
};

class ScrolledWindow : public Widget
{
public:
	ScrolledWindow()
	{
		handle = gtk_scrolled_window_new(0, 0);
	};
	~ScrolledWindow() {}

	template <class W> W* add(W* w)
	{
		gtk_container_add(GTK_CONTAINER(handle), w->handle);
		return w;
	}
};

class Edit : public Widget
{
private:
	std::function<void()> _activate = [](){};

	static void _static_activate(GtkWidget* b, void* data)
	{
		reinterpret_cast<Edit*>(data)->_activate();
	}

public:
	Edit()
	{
		handle = gtk_entry_new();
	}
	~Edit() {}

	template <class F> void onActivate(const F& f)
	{
		_activate=f;
		g_signal_connect(handle, "activate", G_CALLBACK(_static_activate), this);
	}

	void setText(const std::string& text) { gtk_entry_set_text(GTK_ENTRY(handle), text.c_str()); }

	const char* text() { return gtk_entry_get_text(GTK_ENTRY(handle)); }
};

class Menu;

class MenuItem : public Widget
{
private:
	std::function<void()> _activate = [](){};

	static void _static_activate(GtkWidget* w, void* d)
	{
		reinterpret_cast<MenuItem*>(d)->_activate();
	}

public:
	MenuItem(const char* text)
	{
		handle = gtk_menu_item_new_with_label(text);
	}
	~MenuItem() {}

	void addMenu(Menu* menu);

	template <class F> void onActivate(const F& f)
	{
		_activate = f;
		g_signal_connect(G_OBJECT(handle), "activate", G_CALLBACK(_static_activate), this);
	}
};

class Menu : public Widget
{
public:
	Menu()
	{
		handle = gtk_menu_new();
	}

	~Menu() {}

	MenuItem* add(MenuItem* item)
	{
		widgets.push_back(item);
		gtk_menu_shell_append(GTK_MENU_SHELL(handle), item->handle);
		return item;
	}
};

class MenuBar : public Widget
{
public:
	MenuBar()
	{
		handle = gtk_menu_bar_new();
	}

	~MenuBar() {}

	MenuItem* add(MenuItem* item)
	{
		widgets.push_back(item);
		gtk_menu_shell_append(GTK_MENU_SHELL(handle), item->handle);
		return item;
	}
};
