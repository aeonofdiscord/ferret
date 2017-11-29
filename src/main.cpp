#include <iostream>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <fstream>
#include "net.h"
#include "str.h"
#include "worker.h"
#include "ui.h"

enum NodeType
{
	TYPE_DIR,
	TYPE_TEXT,
	TYPE_FILE,
	TYPE_BINARY,
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
	int start, end;
	std::string url;
};

const size_t npos = std::string::npos;
const int SEARCHBOX_HEIGHT = 28;
const char* HOME = "gopher://gopher.quux.org";
int ICON_SIZE = 24;

std::vector<std::string> lines;
std::vector<Node> nodes;
std::vector<History> history;
std::vector<Link> links;
std::string location;

std::unique_ptr<Application> app;
std::unique_ptr<Window> w;
TextView* view = 0;
Edit* address = 0;

const char* userHome()
{
	return getenv("HOME");
}

int docType(int code)
{
	switch(code)
	{
	case 'i':
		return TYPE_TEXT;
	case '1':
		return TYPE_DIR;
	case '0':
		return TYPE_FILE;
	case '4':
	case '5':
	case '6':
	case '9':
	case 'g':
	case 'I':
	case 's':
		return TYPE_BINARY;
	case '7':
		return TYPE_SEARCH;
	default:
		return TYPE_UNKNOWN;
	}
}

void parseList()
{
	nodes.clear();
	for(std::string& line : lines)
	{
		if(line.size() > 1)
		{
			Node n;
			n.type = TYPE_TEXT;
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
			if((n.type != TYPE_TEXT) && parts.size() >= 4)
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
			if(n.code != 'i')
				n.text = std::string(&n.code, 1) + " " + n.text;
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

void showText(const std::string& data)
{
	nodes.clear();

	std::vector<std::string> lines;
	lines.push_back("");
	for(size_t i = 0; i < data.size(); ++i)
	{
		if(data[i] == '\n')
		{
			lines.back() += "\n";
			lines.push_back("");
		}
		else if(data[i] == '.' && i == data.size()-1)
			break;
		else if(data[i] != '\r')
			lines.back() += data[i];
	}
	for(auto& l : lines)
	{
		Node n;
		n.type = TYPE_TEXT;
		n.text = l;
		nodes.push_back(n);
	}
}

bool syncDownload(const std::string& host, const std::string& port, const std::string& req, std::string& data)
{
	auto r = opensocket(host.c_str(), port.c_str());
	if(r.result == -1)
	{
		std::cerr << r.error << "\n";
		showText(r.error);
		return false;
	}
	int s = r.result;

	int opt = 1;
	setsockopt(s, SOL_TCP, TCP_NODELAY, &opt, sizeof(opt));

	int e = send(s, req.c_str(), req.size(), 0);
	if(e == -1)
	{
		std::cerr << strerror(errno) << "\n";
		showText(strerror(errno));
		return false;
	}
	char buffer[4096];
	while(true)
	{
		int bytes = recv(s, buffer, 4096, 0);
		if(bytes == -1)
		{
			std::cout << strerror(errno) << "\n";
			showText(strerror(errno));
			close(s);
			return false;
		}
		else if(bytes)
			data.append(buffer, buffer+bytes);
		else
			break;
	}
	close(s);
	return true;
}

void addNodes(TextView* view);
void go(const char* url, bool addToHistory = true)
{
	if(addToHistory)
		history.push_back({url});
	address->setText(url);
	nodes.clear();
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
	if(type == TYPE_BINARY)
	{
		std::string path = req;
		path.erase(0, path.find("/")+1);
		path.erase(0, path.rfind("/")+1);
		path = std::string(userHome())+"/Downloads/"+path;
		download(url, strip(path));
		showText(std::string("Downloading ") + url + " to " + path);
	}
	else
	{
		location = url;
		address->setText(location);
		std::string data;
		if(syncDownload(host, port, req, data))
		{
			lines.clear();
			if(type == TYPE_DIR)
			{
				while(data.size())
				{
					size_t l = data.find("\n");
					if(l != std::string::npos)
					{
						lines.push_back(data.substr(0, l));
						data.erase(0, l+1);
					}
					else
					{
						lines.push_back(data);
						break;
					}
				}
				parseList();
			}
			else if(type == TYPE_FILE)
			{
				strip(data);
				if(data.size() > 0 && data[data.size()-1] == '.')
					data.erase(data.size()-1);
				showText(data);
			}
		}
	}
	addNodes(view);
}

void goClick()
{
	const char* addr = address->text();
	go(addr, true);
}

void backClick()
{
	if(history.size() > 0)
	{
		std::string url = history.back().url;
		history.pop_back();
		go(url.c_str(), false);
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

void addLink(GtkTextBuffer* buffer, GtkTextTag* tag, const std::string& text, const std::string& url)
{
	GtkTextIter end;
	gtk_text_buffer_get_iter_at_offset(view->buffer, &end, -1);
	int start = gtk_text_iter_get_offset(&end);
	gtk_text_buffer_insert_with_tags(view->buffer, &end, text.c_str(), text.size(), tag, nullptr);
	links.push_back({start, start+int(text.size()), url});
}

void addNodes(TextView* view)
{
	view->clear();
	auto tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(view->buffer), "link");
	for(auto& n : nodes)
	{
		if(n.type == TYPE_TEXT)
			addText(view->buffer, n.text);
		else
			addLink(view->buffer, tag, n.text, n.url);
	}
}

void activate()
{
	auto provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, read("style.css").c_str(), -1, 0);

	w.reset(new Window(app.get()));

	auto screen = gdk_screen_get_default();
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	w->setTitle("ferret");
	w->setDefaultSize(1280, 1024);
	Box* main = w->add(new Box(Box::VERTICAL));
	Box* addressBar = main->insert(new Box(Box::HORIZONTAL));
	Button* back = addressBar->insert(new Button("Back"), false, false);
	back->onClick(backClick);
	//Button* forward = addressBar->insert(new Button("Forward"), false, false);
	Button* up = addressBar->insert(new Button("Up"), false, false);
	up->onClick(upClick);
	address = addressBar->insert(new Edit, true, true);
	address->onActivate(addressBarEnter);
	Button* goURL = addressBar->push(new Button("Go"), false, false);
	goURL->onClick(goClick);

	auto scroll = new ScrolledWindow();
	main->push(scroll, true, true);

	view = scroll->add(new TextView());
	view->setMargin(10, 10, 10, 10);

	addNodes(view);

	view->setEditable(false);
	view->showCursor(false);
	w->showAll();

	GdkRGBA blue = {0, 0, 1, 1} ;
	auto tag = gtk_text_buffer_create_tag(view->buffer, "link", "foreground-rgba", &blue, nullptr);
	g_signal_connect(tag, "event", G_CALLBACK(tagEvent), 0);

	if(std::string(HOME) != "")
	{
		go(HOME);
	}
}

int main(int argc, char** argv)
{
	app.reset(new Application("test.app", 0));
	app->onActivate(activate);

	std::thread worker(runWorker);
	int status = app->run(argc, argv);
	endWorker();
	worker.join();
	return status;
}

