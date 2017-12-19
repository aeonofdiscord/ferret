#include <iostream>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <fstream>
#include "net.h"
#include "queue.h"
#include "str.h"
#include "worker.h"
#include "ui.h"

#ifndef RESOURCE_PATH
	#define RESOURCE_PATH ./share/
#endif

#define _stringify(x) #x
#define stringify(x) _stringify(x)

const std::string RES_PATH = stringify(RESOURCE_PATH);

enum NodeType
{
	TYPE_DIR,
	TYPE_INFO,
	TYPE_FILE,
	TYPE_BINARY,
	TYPE_IMAGE,
	TYPE_AUDIO,
	TYPE_SEARCH,
	TYPE_UNKNOWN,
	TYPE_MAX,
};

enum Mode
{
	MODE_DIR,
	MODE_FILE,
	MODE_BINARY,
};

struct Node
{
	int type = TYPE_UNKNOWN;
	char code;
	std::string text;
	std::string path;
	std::string host;
	std::string port;
	std::string url;
	int start, end;
};

struct History
{
	std::string url;
};

struct Link
{
	enum Type { LINK, SEARCH } type;
	int start, end;
	std::string url;
};

const size_t npos = std::string::npos;
const char* HOME = "gopher://gopher.quux.org";

std::vector<Node> nodes;
std::vector<Link> links;

std::vector<History> history;
int historyPos = 0;

std::string location;
int displayType = TYPE_DIR;

MessageQueue dataQueue;

class SearchDialog;

std::unique_ptr<Application> app;
std::unique_ptr<Window> w;
std::unique_ptr<SearchDialog> searchDialog;

TextView* view = 0;
Edit* address = 0;
int currentRequest = 0;
std::string data;
std::string incompleteData;

GdkPixbuf* icons[TYPE_MAX];

const char* userHome()
{
	return getenv("HOME");
}

int docType(int code)
{
	switch(code)
	{
	case 'i':
		return TYPE_INFO;
	case '1':
		return TYPE_DIR;
	case '0':
		return TYPE_FILE;
	case '4':
	case '5':
	case '6':
	case '9':
		return TYPE_BINARY;
	case 'g':
	case 'I':
		return TYPE_IMAGE;
	case 's':
		return TYPE_AUDIO;
	case '7':
		return TYPE_SEARCH;
	default:
		return TYPE_UNKNOWN;
	}
}

void parseList(const std::string& data, std::vector<Node>& nodes)
{
	if(!nodes.size())
	{
		Node blank;
		blank.type = TYPE_INFO;
		blank.code = 'i';
		blank.text = "\n";
		nodes.push_back(blank);
	}
	std::vector<std::string> lines;
	splitLines(data, lines);
	for(std::string& line : lines)
	{
		if(line.size() > 1)
		{
			Node n;
			n.type = TYPE_INFO;
			char c = line[0];
			n.code = c;
			n.type = docType(c);
			std::vector<std::string> parts;
			line.erase(0, 1);
			while(line.size())
			{
				auto i = line.find('\t');
				parts.push_back(line.substr(0, i));
				if(i == std::string::npos)
					line.clear();
				else
					line.erase(0, i+1);
			}
			if((n.type != TYPE_INFO) && parts.size() >= 4)
			{
				n.path = parts[1];
				if(n.path[0] != '/')
					n.path.insert(0, "/");
				n.host = parts[2];
				n.port = strip(parts[3]);
				if(n.port != "70")
					n.url = "gopher://"+(n.host + ":" + n.port + "/" + n.code + n.path);
				else
					n.url = "gopher://"+(n.host + "/" + n.code + n.path);
			}
			n.text = parts[0] + "\n";
			nodes.push_back(n);
		}
	}
}

std::string filetype(const std::string& path)
{
	auto i = path.rfind(".");
	if(i == npos)
		return "";
	return path.substr(i);
}

void showText(const std::string& data, std::vector<Node>& nodes)
{
	if(!nodes.size())
	{
		Node blank;
		blank.type = TYPE_INFO;
		blank.code = 'i';
		blank.text = "\n";
		nodes.push_back(blank);
	}
	std::vector<std::string> lines;
	splitLines(data, lines);
	for(auto& l : lines)
	{
		Node n;
		n.type = TYPE_INFO;
		n.text = l + "\n";
		nodes.push_back(n);
	}
}

void showNodes(TextView* view, const std::vector<Node>& nodes);

void showMessage(const std::string& data)
{
	nodes.clear();
	links.clear();
	showText(data, nodes);
	showNodes(view, nodes);
}

void queueData(const Message& m)
{
	dataQueue.push(m);
}

void go(const char* url, bool addToHistory = true, bool clearFuture = true)
{
	view->clear();
	data = "";
	if(addToHistory)
	{
		if(history.size() && clearFuture)
		{
			history.erase(history.begin() + historyPos, history.end());
		}
		++historyPos;
		history.push_back({url});
	}
	address->setText(url);
	nodes.clear();
	links.clear();
	std::string addr = url;
	if(addr.find("gopher://")==0)
	{
		addr = addr.substr(9);
	}
	std::string host = addr;
	host = host.substr(0, host.find('/'));
	host = host.substr(0, host.rfind(':'));

	std::string req = addr;
	char code = '1';
	auto sl = req.find('/');
	if(sl == npos)
		req = "";
	else if(req.size() > sl+1)
	{
		code = req[sl+1];
		req.erase(0, sl+3);
	}
	req += "\r\n";

	std::string port = "70";
	if(addr.find(":") != std::string::npos)
	{
		port = addr.substr(addr.find(":")+1);
		port = port.substr(0, port.rfind("/"));
	}
	int type = docType(code);
	if(type == TYPE_BINARY || type == TYPE_IMAGE || type == TYPE_AUDIO)
	{
		std::string path = req;
		path.erase(0, path.find("/")+1);
		path.erase(0, path.rfind("/")+1);
		path = std::string(userHome())+"/Downloads/"+path;
		download(url, strip(path));
		showMessage(std::string("Downloading ") + url + " to " + path);
	}
	else
	{
		location = url;
		address->setText(location);
		fetch(++currentRequest, location, type);
		displayType = type;
	}
}

void goClick()
{
	const char* addr = address->text();
	go(addr, true);
}

void backClick()
{
	if(historyPos > 1)
	{
		--historyPos;
		std::string url = history[historyPos-1].url;
		go(url.c_str(), false);
	}
}

void forwardClick()
{
	if(historyPos < int(history.size()))
	{
		++historyPos;
		std::string url = history[historyPos-1].url;
		go(url.c_str(), false, false);
	}
}

void upClick()
{
	std::string addr = location;
	if(addr.find("gopher://") == 0)
		addr.erase(0, 9);
	size_t sl = addr.find("/");
	std::string host = addr.substr(0, sl);
	if(sl == npos || sl == addr.size()-1)
	{
		return;
	}
	std::string target = "gopher://"+host;
	std::string path = addr.substr(sl+2);
	path.erase(path.rfind("/"));
	if(path != "")
	{
		target += "/1" + path;
	}
	go(target.c_str());
}

void addressBarEnter()
{
	std::string addr = address->text();
	if(addr.find("gopher://") != 0)
		addr = "gopher://" + addr;
	go(addr.c_str());
}

std::string read(const char* path)
{
	std::string str;
	std::ifstream file(path);
	while(file.good())
	{
		char c = file.get();
		if(!file.good())
			break;
		str += c;
	}
	return str;
}

struct SearchDialog : public Widget
{
	std::string url;
	Edit* text;

	static void _response(GtkDialog* dialog, int response, gpointer data)
	{
		auto d = static_cast<SearchDialog*>(data);
		if(response == GTK_RESPONSE_OK)
		{
			d->search();
		}
		d->destroy();
	}

	static void _destroy(GtkDialog* dialog, int response, gpointer data)
	{
		searchDialog.reset(0);
	}

	SearchDialog(const std::string& url) : url(url), text(0)
	{
		handle = GTK_WIDGET(gtk_dialog_new_with_buttons ("Search", GTK_WINDOW(w->handle), GTK_DIALOG_MODAL,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			nullptr));
		auto content_area = gtk_dialog_get_content_area(GTK_DIALOG(handle));
		gtk_container_add(GTK_CONTAINER(content_area), gtk_label_new("Search query: "));
		text = new Edit();
		text->onActivate([this](){
			search();
			destroy();
		});
		gtk_container_add(GTK_CONTAINER(content_area), text->handle);
		widgets.push_back(text);
		g_signal_connect(GTK_DIALOG(handle), "response", G_CALLBACK(_response), this);
		g_signal_connect(GTK_DIALOG(handle), "destroy", G_CALLBACK(_destroy), this);
		gtk_widget_show_all(handle);
	}

	~SearchDialog()
	{

	}

	void destroy()
	{
		gtk_widget_destroy(handle);
		handle = 0;
	}

	void search()
	{
		go((url + "\t" + text->text()).c_str());
	}
};

void tagEvent(GtkTextTag* tag, GObject* o, GdkEvent* event, GtkTextIter* iter, gpointer data)
{
	if(event->type == GDK_BUTTON_PRESS)
	{
		int offset = gtk_text_iter_get_offset(iter);
		for(auto& l : links)
		{
			if(l.url == "")
				continue;
			if(offset >= l.start && offset < l.end)
			{
				if(l.type == Link::SEARCH)
				{
					searchDialog.reset(new SearchDialog(l.url));
				}
				else
					go(l.url.c_str());
				break;
			}
		}
	}
}

void addText(GtkTextBuffer* buffer, const std::string& text)
{
	GtkTextIter end;
	gtk_text_buffer_get_iter_at_offset(view->buffer, &end, -1);
	gtk_text_buffer_insert(view->buffer, &end, text.c_str(), text.size());
}

void addLink(GtkTextBuffer* buffer, const std::string& text, const std::string& url, GdkPixbuf* icon = 0, Link::Type type = Link::LINK)
{
	auto link = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(view->buffer), "link");

	GtkTextIter end;
	gtk_text_buffer_get_iter_at_offset(view->buffer, &end, -1);
	int startOffset = gtk_text_iter_get_offset(&end);

	if(icon)
	{
		auto iconTag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(view->buffer), "icon");
		gtk_text_buffer_insert_pixbuf(view->buffer, &end, icon);

		GtkTextIter start;
		gtk_text_buffer_get_iter_at_offset(view->buffer, &start, startOffset);

		gtk_text_buffer_get_iter_at_offset(view->buffer, &end, -1);
		gtk_text_buffer_apply_tag(view->buffer, iconTag, &start, &end);
	}
	gtk_text_buffer_insert(view->buffer, &end, "    ", 4);
	gtk_text_buffer_get_iter_at_offset(view->buffer, &end, -1);
	int linkOffset = gtk_text_iter_get_offset(&end);
	gtk_text_buffer_insert_with_tags(view->buffer, &end, text.c_str(), text.size(), link, nullptr);
	links.push_back({type, linkOffset, linkOffset+int(text.size()-1), url});
}

void showNodes(TextView* view, const std::vector<Node>& nodes)
{
	for(auto& n : nodes)
	{
		if(n.type == TYPE_INFO)
			addText(view->buffer, n.text);
		else
			addLink(view->buffer, n.text, n.url, icons[n.type], n.type == TYPE_SEARCH? Link::SEARCH : Link::LINK);
	}
}

void quit()
{
	app->quit();
}

void save()
{
	auto save = GTK_FILE_CHOOSER(gtk_file_chooser_dialog_new("Save", GTK_WINDOW(w->handle), GTK_FILE_CHOOSER_ACTION_SAVE,
			"_Save", GTK_RESPONSE_ACCEPT,
			"_Cancel", GTK_RESPONSE_CANCEL,
			nullptr));
	gtk_file_chooser_set_do_overwrite_confirmation((save), true);
	gtk_file_chooser_set_current_name(save, "untitled.txt");
	auto res = gtk_dialog_run(GTK_DIALOG(save));
	if(res == GTK_RESPONSE_ACCEPT)
	{
		std::string filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(save));
		std::ofstream file(filename.c_str());
		file.write(data.c_str(), data.size());
	}
	gtk_widget_destroy(GTK_WIDGET(save));
}

GdkPixbuf* loadImage(const std::string& path, float scale = 1.0f)
{
	auto p = gdk_pixbuf_new_from_file((RES_PATH+path).c_str(), 0);
	if(!p)
	{
		std::cerr << "Failed to load image: " << RES_PATH+path << "\n";
		return 0;
	}
	if(scale == 1.0f)
		return p;
	auto scaled = gdk_pixbuf_scale_simple(p, gdk_pixbuf_get_width(p) * scale, gdk_pixbuf_get_height(p) * scale, GDK_INTERP_NEAREST);
	g_object_unref(p);
	return scaled;
}

void activate()
{
	w.reset(new Window(app.get()));

	w->setTitle("ferret");
	w->setDefaultSize(1280, 1024);
	auto provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, read((RES_PATH + "style.css").c_str()).c_str(), -1, 0);
	auto screen = gdk_screen_get_default();
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);


	Box* main = w->add(new Box(Box::VERTICAL));
	auto context = gtk_widget_get_style_context(main->handle);
	gtk_style_context_add_class(context, "main");

	auto menubar = main->insert(new MenuBar());
	auto fileMenu = new Menu();
	auto fileMi = menubar->add(new MenuItem("File"));
	auto saveMi = fileMenu->add(new MenuItem("Save"));
	saveMi->onActivate(save);
	auto quitMi = fileMenu->add(new MenuItem("Quit"));
	fileMi->addMenu(fileMenu);
	quitMi->onActivate(quit);

	Box* addressBar = main->insert(new Box(Box::HORIZONTAL));
	Button* back = addressBar->insert(new Button("Back"), false, false);
	back->onClick(backClick);
	Button* forward = addressBar->insert(new Button("Forward"), false, false);
	forward->onClick(forwardClick);
	Button* up = addressBar->insert(new Button("Up"), false, false);
	up->onClick(upClick);
	address = addressBar->insert(new Edit, true, true);
	address->onActivate(addressBarEnter);
	Button* goURL = addressBar->push(new Button("Go"), false, false);
	goURL->onClick(goClick);

	auto scroll = new ScrolledWindow();
	main->push(scroll, true, true);

	view = scroll->add(new TextView());
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view->handle), 20);

	view->setEditable(false);
	view->showCursor(false);
	w->showAll();

	GdkRGBA blue = {0, 0, 1, 1} ;
	gtk_text_buffer_create_tag(view->buffer, "icon",
		"rise", -7 * PANGO_SCALE,
		"rise-set", true,
		nullptr);
	auto link = gtk_text_buffer_create_tag(view->buffer, "link",
		"underline", true,
		"foreground-rgba", &blue,
		nullptr);

	g_signal_connect(link, "event", G_CALLBACK(tagEvent), 0);

	icons[TYPE_DIR] = loadImage("icons/directory.png");
	icons[TYPE_FILE] = loadImage("icons/text.png");
	icons[TYPE_BINARY] = loadImage("icons/binary.png");
	icons[TYPE_IMAGE] = loadImage("icons/image.png");
	icons[TYPE_AUDIO] = loadImage("icons/audio.png");
	icons[TYPE_SEARCH] = loadImage("icons/search.png");
	icons[TYPE_UNKNOWN] = loadImage("icons/unknown.png");

	icons[TYPE_MAX] = 0;

	if(std::string(HOME) != "")
	{
		go(HOME);
	}
}

void popQueue(std::vector<Node>& nodes)
{
	if(!dataQueue.size())
		return;
	auto m = dataQueue.pop();
	if(m.reqid == currentRequest)
	{
		if(m.type == Message::DATA)
		{
			std::string completeData;
			incompleteData += m.data;
			auto end = incompleteData.rfind("\n");
			completeData = incompleteData.substr(0, end);
			incompleteData = incompleteData.substr(end+1);

			data += completeData;
			if(displayType == TYPE_DIR || displayType == TYPE_SEARCH)
			{
				parseList(completeData, nodes);
			}
			else
			{
				showText(completeData, nodes);
			}
		}
		else if(m.type == Message::ERROR)
		{
			showText(m.data, nodes);
		}
	}
	else
		std::cout << "discarded " << m.data.size() << " bytes from req " << m.reqid << "\n";
}

int idle(void*)
{
	if(!dataQueue.size())
		return 1;
	std::vector<Node> nodes;
	while(dataQueue.size())
	{
		popQueue(nodes);
	}
	showNodes(view, nodes);
	return 1;
}

void cleanup()
{
	for(auto p : icons)
	{
		if(p) g_object_unref(p);
	}
}

int main(int argc, char** argv)
{
	app.reset(new Application("test.app", 0));
	app->onActivate(activate);

	std::thread worker(runWorker);
	g_timeout_add(10, idle, 0);
	int status = app->run(argc, argv);
	endWorker();
	worker.join();
	cleanup();
	return status;
}

