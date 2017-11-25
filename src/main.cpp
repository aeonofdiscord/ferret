#include <libui/ui.h>
#include <iostream>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <cassert>
#include <memory>
#include <thread>
#include <fstream>
#include "net.h"
#include "worker.h"

const size_t npos = std::string::npos;
const int SEARCHBOX_HEIGHT = 28;
const char* HOME = "gopher://gopher.quux.org";
int ICON_SIZE = 24;

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
	double x = 0, y = 0, w = 0, h = 0;
};

struct
{
	int text = 0;
	int link = 0x0000ff;
} colours;

static uiWindow *window;

enum Style
{
	STYLE_NORMAL = 0,
	STYLE_ITALIC = 1,
	STYLE_BOLD   = 2,
};

enum Font
{
	FONT_TEXT,
	FONT_LINK,
	FONT_MAX,
};

struct History
{
	std::string url;
};

uiDrawTextFont* fonts[FONT_MAX];

struct Colour { double r, g, b, a; };

std::vector<std::string> lines;
std::vector<Node> nodes;
std::vector<History> history;
uiArea* area = 0;
double areaW = 0, areaH = 0;
std::string location;

const char* userHome()
{
	return getenv("HOME");
}

static int onClosing(uiWindow *w, void *data)
{
	uiQuit();
	return 1;
}

static int onShouldQuit(void *data)
{
	uiWindow *mainwin = uiWindow(data);

	uiControlDestroy(uiControl(mainwin));
	return 1;
}

Colour rgb(int c)
{
	Colour colour;
	colour.r = (c >> 16) / 255.0;
	colour.g = ((c >> 8) & 0xff) / 255.0;
	colour.b = (c & 0xff) / 255.0;
	colour.a = 1.0;
	return colour;
}

uiDrawTextFont* loadFont(const char* name, double size, int style = 0)
{
	uiDrawTextFontDescriptor desc;
	desc.Family = name;
	desc.Italic = style & STYLE_ITALIC ? uiDrawTextItalicItalic : uiDrawTextItalicNormal;
	desc.Stretch = uiDrawTextStretchNormal;
	desc.Weight = style & STYLE_BOLD ? uiDrawTextWeightBold : uiDrawTextWeightNormal;
	desc.Size = size;
	return uiDrawLoadClosestFont(&desc);
}

int windowW()
{
	int w, h;
	uiWindowContentSize(window, &w, &h);
	return w;
}

std::string replaceAll(std::string& str, const std::string& f, const std::string& r)
{
	auto i = str.find(f);
	while(str.find(f) != npos)
	{
		str.replace(i, f.size(), r);
		i = str.find(f);
	}
	return str;
}

void drawText(uiDrawContext* context, int x, int y, const char* text, uiDrawTextFont* font, int colour = -1)
{
	if(colour == -1)
		colour = colours.text;

	Colour c = rgb(colour);
	size_t len = strlen(text);
	uiDrawTextLayout* layout = uiDrawNewTextLayout(text, font, windowW());
	uiDrawTextLayoutSetColor(layout, 0, len, c.r, c.g, c.b, 1.0);
	uiDrawText(context, x, y, layout);
	uiDrawFreeTextLayout(layout);
}

void textExtents(const char* text, uiDrawTextFont* font, double* w, double* h)
{
	uiDrawTextLayout* layout = uiDrawNewTextLayout(text, font, windowW());
	uiDrawTextLayoutExtents(layout, w, h);
	uiDrawFreeTextLayout(layout);
}

void fillArea(uiAreaDrawParams* p, int colour)
{
	uiDrawBrush brush;
	brush.Type = uiDrawBrushTypeSolid;
	brush.R = (colour >> 16) / 255.0;
	brush.G = ((colour >> 8) & 0xff) / 255.0;
	brush.B = (colour & 0xff) / 255.0;
	brush.A = 1.0f;
	uiDrawPath* path = uiDrawNewPath(uiDrawFillModeWinding);
	double h = std::max(p->ClipHeight, areaH);
	uiDrawPathAddRectangle(path, 0, 0, p->ClipWidth, h);
	uiDrawPathEnd(path);
	uiDrawFill(p->Context, path, &brush);
	uiDrawFreePath(path);
}

void drawNode(const Node& n, uiDrawContext* context)
{
	auto font = fonts[FONT_TEXT];
	int colour = colours.text;
	if(n.type != TYPE_TEXT)
	{
		font = fonts[FONT_LINK];
		colour = colours.link;
		drawText(context, n.x, n.y, n.text.c_str(), font, colour);
	}
	else
		drawText(context, n.x, n.y, n.text.c_str(), font, colour);
	if(n.type == TYPE_SEARCH)
	{
	}
}

void areaDraw(uiAreaHandler*, uiArea*, uiAreaDrawParams* p)
{
	fillArea(p, 0xffffff);
	for(const Node& n : nodes)
		drawNode(n, p->Context);
}

void areaDragBroken(uiAreaHandler*, uiArea*)
{

}

int areaKeyEvent(uiAreaHandler*, uiArea*, uiAreaKeyEvent*)
{
	return 0;
}

void areaMouseCrossed(uiAreaHandler*, uiArea*, int)
{

}

void go(const char* url, const char* params = 0);
void areaMouseEvent(uiAreaHandler*, uiArea*, uiAreaMouseEvent* e)
{
	if(e->Down)
	{
		for(Node& n : nodes)
		{
			if(n.x <= e->X && n.y <= e->Y && n.x + n.w > e->X && n.y+n.h > e->Y)
			{
				if(n.type == TYPE_TEXT)
					return;
				history.push_back({n.url});
				go(n.url.c_str());
				return;
			}
		}
	}
}

uiEntry* address = 0;

void slice(std::string& str, size_t start, size_t end = std::string::npos)
{
	if(start > str.size())
		start = 0;
	str.erase(0, start);
	if(end > str.size())
		end = str.size();
	str.erase(end, str.size());
}

std::string rstrip(std::string& s)
{
	while(s.size() > 0 && isspace(s[s.size()-1]))
		s.erase(s.size()-1);
	return s;
}

std::string lstrip(std::string& s)
{
	while(s.size() > 0 && isspace(s[0]))
		s.erase(0, 1);
	return s;
}

std::string strip(std::string& s)
{
	lstrip(s);
	rstrip(s);
	return s;
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
	case's':
		return TYPE_BINARY;
	case '7':
		return TYPE_SEARCH;
	default:
		return TYPE_UNKNOWN;
	}
}

void parseList()
{
	int margin = 10;
	int linespace = 0;
	int py = margin;

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
			auto font = fonts[FONT_TEXT];
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
				font = fonts[FONT_LINK];
			}
			n.text = parts[0];
			if(n.code != 'i')
				n.text = std::string(&n.code, 1) + " " + n.text;
			auto layout = uiDrawNewTextLayout(n.text.c_str(), font, windowW());
			uiDrawTextLayoutExtents(layout, &n.w, &n.h);
			uiDrawFreeTextLayout(layout);
			n.x = margin;
			if(n.type != TYPE_TEXT)
			{
				//n.x += ICON_SIZE;
			}
			n.y = py;
			nodes.push_back(n);
			py += n.h + linespace;
			if(n.type == TYPE_SEARCH)
			{
				py += SEARCHBOX_HEIGHT;
			}
		}
	}
	areaH = py;
	uiAreaSetSize(area, areaW || 900, areaH);
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
			lines.push_back("");
		else if(data[i] == '.' && i == data.size()-1)
			break;
		else if(data[i] != '\r')
			lines.back() += data[i];

	}
	int py = 10;
	for(auto& l : lines)
	{
		Node n;
		n.x = 10;
		n.y = py;
		n.type = TYPE_TEXT;
		n.text = l;
		uiDrawTextLayout* layout = uiDrawNewTextLayout(l.c_str(), fonts[FONT_TEXT], windowW());
		double w, h;
		uiDrawTextLayoutExtents(layout, &w, &h);
		py += h;
		uiDrawFreeTextLayout(layout);
		nodes.push_back(n);
	}

	areaH = py;
	uiAreaSetSize(area, areaW || 900, areaH);
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

void go(const char* url, const char* params)
{
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
		uiEntrySetText(address, location.c_str());
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
				showText(data);
			}
		}

		if(area)
			uiAreaQueueRedrawAll(area);
	}
}

void goClick(uiButton* b, void* data)
{
	char* addr = uiEntryText(address);
	history.push_back({addr});
	go(addr);
}

void backClick(uiButton* b, void* data)
{
	if(history.size() > 1)
	{
		history.pop_back();
		go(history.back().url.c_str());
	}
}

template <class T> void fmt(const std::string& f, const T& values)
{
	std::string result = "";
	bool escape = false;
	for(size_t i = 0; i < f.size(); ++i)
	{
		if(f[i] == '\\')
		{
			escape = true;
			continue;
		}
		if(escape)
		{
			result += f[i];
		}
		else if(f[i] == '$')
		{

		}
		escape = false;
	}
}

void upClick(uiButton* b, void* data)
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

void addressBarEnter(uiEntry* e, void* data)
{
	std::string addr = uiEntryText(e);
	if(addr.find("gopher://") != 0)
		addr = "gopher://" + addr;
	history.push_back({addr});
	go(addr.c_str());
}

int main(void)
{
	uiInitOptions options;
	options.Size = 0;
	const char* err = uiInit(&options);
	if (err) {
		std::cerr << "error initializing libui: " << err << "\n";
		uiFreeInitError(err);
		return 1;
	}

	fonts[FONT_TEXT] = loadFont("Sans", 11, STYLE_NORMAL);
	fonts[FONT_LINK] = loadFont("Sans", 11, STYLE_ITALIC);

	window = uiNewWindow("ferret", 1280, 1024, 0);
	uiWindowOnClosing(window, onClosing, NULL);
	uiOnShouldQuit(onShouldQuit, window);

	uiBox* box = uiNewVerticalBox();
	uiWindowSetChild(window, uiControl(box));

	uiBox* addressBar = uiNewHorizontalBox();
	uiBoxAppend(box, uiControl(addressBar), 0);

	uiButton* back = uiNewButton("Back");
	uiButtonOnClicked(back, backClick, 0);
	uiBoxAppend(addressBar, uiControl(back), 0);

	//uiButton* forward = uiNewButton("Forward");
	//uiButtonOnClicked(forward, forwardClick, 0);
	//uiBoxAppend(addressBar, uiControl(forward), 0);

	uiButton* up = uiNewButton("Up");
	uiButtonOnClicked(up, upClick, 0);
	uiBoxAppend(addressBar, uiControl(up), 0);

	address = uiNewEntry();
	uiEntryOnEnter(address, addressBarEnter, 0);
	uiBoxAppend(addressBar, uiControl(address), 1);

	uiButton* goButton = uiNewButton("Go");
	uiButtonOnClicked(goButton, goClick, 0);
	uiBoxAppend(addressBar, uiControl(goButton), 0);

	uiAreaHandler handler;
	handler.Draw = areaDraw;
	handler.DragBroken = areaDragBroken;
	handler.KeyEvent = areaKeyEvent;
	handler.MouseCrossed = areaMouseCrossed;
	handler.MouseEvent = areaMouseEvent;

	uiControlShow(uiControl(window));
	int w, h;
	uiWindowContentSize(window, &w, &h);
	area = uiNewScrollingArea(&handler, areaW, areaH);
	uiBoxAppend(box, uiControl(area), 1);

	std::thread worker(runWorker);
	if(std::string(HOME) != "")
	{
		history.push_back({HOME});
		go(HOME);
	}
	uiMain();
	endWorker();
	worker.join();
	return 0;
}

