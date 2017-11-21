#include <libui/ui.h>
#include <iostream>
#include <memory.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <cassert>

const size_t npos = std::string::npos;
const int SEARCHBOX_HEIGHT = 28;
const char* HOME = "";

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
	std::string text;
	std::string path;
	std::string host;
	std::string port;
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
	int type;
};

uiDrawTextFont* fonts[FONT_MAX];

struct Colour { double r, g, b, a; };

std::vector<std::string> lines;
std::vector<Node> nodes;
std::vector<History> history;
uiArea* area = 0;
double areaW = 0, areaH = 0;

std::string location;

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

	std::string t = text;
	if(t[t.size()-1] == '\n')
		t.erase(t.size()-1);

	Colour c = rgb(colour);
	uiDrawTextLayout* layout = uiDrawNewTextLayout(t.c_str(), font, windowW());
	uiDrawTextLayoutSetColor(layout, 0, t.size(), c.r, c.g, c.b, 1.0);
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

void areaDraw(uiAreaHandler*, uiArea*, uiAreaDrawParams* p)
{
	fillArea(p, 0xffffff);
	for(const Node& n : nodes)
	{
		auto font = fonts[FONT_TEXT];
		int colour = colours.text;
		if(n.type != TYPE_TEXT)
		{
			font = fonts[FONT_LINK];
			colour = colours.link;
		}
		drawText(p->Context, n.x, n.y, n.text.c_str(), font, colour);
		if(n.type == TYPE_SEARCH)
		{
		}
	}
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

void go(const char* url, int type = TYPE_DIR, const char* params = 0);
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
				std::string url;
				if(n.port != "70")
					url = (n.host + ":" + n.port + n.path);
				else
					url = (n.host + n.path);
				history.push_back({url, n.type});
				go(url.c_str(), n.type);
				return;
			}
		}
	}
}

uiEntry* address = 0;

struct Result
{
	int result;
	const char* error;
};

Result opensocket(const char* address, const char* port)
{
	addrinfo info;
	memset(&info, 0, sizeof(info));
	info.ai_family = AF_INET;
	info.ai_socktype = SOCK_STREAM;
	addrinfo* res;
	int e_addr = getaddrinfo(address, port, &info, &res);
	if(e_addr < 0)
	{
		return {-1, "Could not open address" };
	}

	int client = socket(AF_INET, SOCK_STREAM, 0);
	if(client == -1)
	{
		return {-1, strerror(errno) };
	}

	if(!res)
	{
		close(client);
		return {-1, strerror(errno) };
	}

	int flags = fcntl(client, F_GETFL);
	fcntl(client, F_SETFL, flags | O_NONBLOCK);
	connect(client, res->ai_addr, res->ai_addrlen);

	fd_set wr;
	float timeout = 10.0f * 1000000;
	while(timeout > 0)
	{
		FD_ZERO(&wr);
		FD_SET(client, &wr);

		timeval t;
		t.tv_sec = 0;
		t.tv_usec = 1;
		select(client+1, 0, &wr, 0, &t);
		int i = 0;
		if(FD_ISSET(client, &wr) || i)
		{
			flags = fcntl(client, F_GETFL);
			fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
			return {client, ""};
		}
		usleep(10000);
		timeout -= 10000;
	}
	close(client);
	return {-1, "Timeout" };
}

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
			if(c == 'i')
				n.type = TYPE_TEXT;
			else if(c == '1')
				n.type = TYPE_DIR;
			else if(c == '0')
				n.type = TYPE_FILE;
			else if(c == '4' || c == '5' || c == '6' || c == '9' || c == 'g' || c == 'I')
				n.type = TYPE_BINARY;
			else if(c == '7')
				n.type = TYPE_SEARCH;
			else
				n.type = TYPE_UNKNOWN;
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
				font = fonts[FONT_LINK];
			}
			auto layout = uiDrawNewTextLayout(parts[0].c_str(), font, windowW());
			uiDrawTextLayoutExtents(layout, &n.w, &n.h);
			uiDrawFreeTextLayout(layout);
			n.text = parts[0];
			n.x = margin;
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
	Node n;
	n.x = 10;
	n.y = 10;
	n.type = TYPE_TEXT;
	n.text = data;
	replaceAll(n.text, "\r", "");
	rstrip(n.text);
	if(n.text.size() && n.text[n.text.size()-1] == '.')
		n.text.erase(n.text.size()-1);
	nodes.push_back(n);
	auto layout = uiDrawNewTextLayout(data.c_str(), fonts[FONT_TEXT], windowW());
	uiDrawTextLayoutExtents(layout, &n.w, &n.h);
	uiDrawFreeTextLayout(layout);
	areaH = n.h;
	uiAreaSetSize(area, areaW || 900, areaH);
}

void go(const char* url, int type, const char* params)
{
	nodes.clear();
	std::string addr = url;
	std::string host = url;
	host = host.substr(0, host.find('/'));
	host = host.substr(0, host.rfind(':'));

	std::string req = url;
	auto sl = req.find('/');
	if(sl == npos)
		req = "";
	else
		req = req.substr(sl+1);
	req += "\r\n";

	std::string port = "70";
	if(addr.find(":") != std::string::npos)
	{
		port = addr.substr(addr.find(":")+1);
	}

	location = url;
	uiEntrySetText(address, location.c_str());
	auto r = opensocket(host.c_str(), port.c_str());
	if(r.result == -1)
	{
		std::cerr << r.error << "\n";
		showText(r.error);
		return;
	}
	int s = r.result;
	std::string data;
	int e = send(s, req.c_str(), req.size(), 0);
	if(e == -1)
	{
		std::cerr << strerror(errno) << "\n";
		showText(strerror(errno));
		return;
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
			return;
		}
		else if(bytes)
			data.append(buffer, buffer+bytes);
		else
			break;
	}

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

	if(area)
		uiAreaQueueRedrawAll(area);
	close(s);
}

void goClick(uiButton* b, void* data)
{
	char* addr = uiEntryText(address);
	history.push_back({addr, TYPE_DIR});
	go(addr);
}

void backClick(uiButton* b, void* data)
{
	if(history.size() > 1)
	{
		history.pop_back();
		go(history.back().url.c_str(), history.back().type);
	}
}

void addressBarEnter(uiEntry* e, void* data)
{
	history.push_back({uiEntryText(e), TYPE_DIR});
	go(uiEntryText(e));
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

	address = uiNewEntry();
	uiEntryOnEnter(address, addressBarEnter, 0);
	uiBoxAppend(addressBar, uiControl(address), 1);

	uiButton* goButton = uiNewButton("Go");
	uiButtonOnClicked(goButton, goClick, 0);
	uiBoxAppend(addressBar, uiControl(goButton), 0);

	uiButton* back = uiNewButton("Back");
	uiButtonOnClicked(back, backClick, 0);
	uiBoxAppend(addressBar, uiControl(back), 0);

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

	uiMain();
	return 0;
}

