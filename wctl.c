#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include "dat.h"
#include "fns.h"
#include <ctype.h>

char	Ebadwr[]		= "bad rectangle in wctl request";
char	Ewalloc[]		= "window allocation failed in wctl request";

/* >= Top are disallowed if mouse button is pressed */
enum
{
	New,
	Resize,
	Move,
	Scroll,
	Noscroll,
	Set,
	Top,
	Bottom,
	Current,
	Hide,
	Unhide,
	Delete,
	Wallpaper,
};

static char *cmds[] = {
	[New]		= "new",
	[Resize]		= "resize",
	[Move]		= "move",
	[Scroll]		= "scroll",
	[Noscroll]		= "noscroll",
	[Set]			= "set",
	[Top]		= "top",
	[Bottom]		= "bottom",
	[Current]		= "current",
	[Hide]		= "hide",
	[Unhide]		= "unhide",
	[Delete]		= "delete",
	[Wallpaper]	= "wallpaper",
	nil
};

enum
{
	Cd,
	Deltax,
	Deltay,
	Hidden,
	Id,
	Maxx,
	Maxy,
	Minx,
	Miny,
	PID,
	R,
	Scrolling,
	Noscrolling,
	Wpx,
	Wpy,
	Filename,
};

static char *params[] = {
	[Cd]	 			= "-cd",
	[Deltax]			= "-dx",
	[Deltay]			= "-dy",
	[Hidden]			= "-hide",
	[Id]				= "-id",
	[Maxx]			= "-maxx",
	[Maxy]			= "-maxy",
	[Minx]			= "-minx",
	[Miny]			= "-miny",
	[PID]				= "-pid",
	[R]				= "-r",
	[Scrolling]			= "-scroll",
	[Noscrolling]		= "-noscroll",
	[Wpx]			= "-x",
	[Wpy]			= "-y",
	[Filename]		= "-f",
	nil
};

/*
 * Check that newly created window will be of manageable size
 */
int
goodrect(Rectangle r)
{
	if(!eqrect(canonrect(r), r))
		return 0;
	if(Dx(r)<100 || Dy(r)<3*font->height)
		return 0;
	/* must have some screen and border visible so we can move it out of the way */
	if(Dx(r) >= Dx(screen->r) && Dy(r) >= Dy(screen->r))
		return 0;
	/* reasonable sizes only please */
	if(Dx(r) > BIG*Dx(screen->r))
		return 0;
	if(Dy(r) > BIG*Dx(screen->r))
		return 0;
	return 1;
}

static
int
word(char **sp, char *tab[])
{
	char *s, *t;
	int i;

	s = *sp;
	while(isspace(*s))
		s++;
	t = s;
	while(*s!='\0' && !isspace(*s))
		s++;
	for(i=0; tab[i]!=nil; i++)
		if(strncmp(tab[i], t, strlen(tab[i])) == 0){
			*sp = s;
			return i;
	}
	return -1;
}

int
set(int sign, int neg, int abs, int pos)
{
	if(sign < 0)
		return neg;
	if(sign > 0)
		return pos;
	return abs;
}

Rectangle
newrect(void)
{
	static int i = 0;
	int minx, miny, dx, dy;

	dx = min(600, Dx(screen->r) - 2*Borderwidth);
	dy = min(400, Dy(screen->r) - 2*Borderwidth);
	minx = 32 + 16*i;
	miny = 32 + 16*i;
	i++;
	i %= 10;

	return Rect(minx, miny, minx+dx, miny+dy);
}

void
shift(int *minp, int *maxp, int min, int max)
{
	if(*minp < min){
		*maxp += min-*minp;
		*minp = min;
	}
	if(*maxp > max){
		*minp += max-*maxp;
		*maxp = max;
	}
}

Rectangle
rectonscreen(Rectangle r)
{
	shift(&r.min.x, &r.max.x, screen->r.min.x, screen->r.max.x);
	shift(&r.min.y, &r.max.y, screen->r.min.y, screen->r.max.y);
	return r;
}

/* permit square brackets, in the manner of %R */
int
riostrtol(char *s, char **t)
{
	int n;

	while(*s!='\0' && (*s==' ' || *s=='\t' || *s=='['))
		s++;
	if(*s == '[')
		s++;
	n = strtol(s, t, 10);
	if(*t != s)
		while((*t)[0] == ']')
			(*t)++;
	return n;
}


int
parsewctl(char **argp, Rectangle r, Rectangle *rp, int *pidp, int *idp, int *hiddenp, int *scrollingp, int *xscalep, int *yscalep, char **strp, char *s, char *err)
{
	int cmd, param, xy, sign;
	char *t;

	*pidp = 0;
	*hiddenp = 0;
	*scrollingp = scrolling;
	*xscalep = -1;
	*yscalep = -1;
	*strp = nil;
	cmd = word(&s, cmds);
	if(cmd < 0){
		strcpy(err, "unrecognized wctl command");
		return -1;
	}
	if(cmd == New)
		r = newrect();

	strcpy(err, "missing or bad wctl parameter");
	while((param = word(&s, params)) >= 0){
		switch(param){	/* special cases */
		case Hidden:
			*hiddenp = 1;
			continue;
		case Scrolling:
			*scrollingp = 1;
			continue;
		case Noscrolling:
			*scrollingp = 0;
			continue;
		case R:
			r.min.x = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			r.min.y = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			r.max.x = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			r.max.y = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			continue;
		}
		while(isspace(*s))
			s++;
		if(param == Cd || param == Filename){
			*strp = s;
			while(*s && !isspace(*s))
				s++;
			if(*s != '\0')
				*s++ = '\0';
			continue;
		}
		sign = 0;
		if(*s == '-'){
			sign = -1;
			s++;
		}else if(*s == '+'){
			sign = +1;
			s++;
		}
		if(!isdigit(*s))
			return -1;
		xy = riostrtol(s, &s);
		switch(param){
		case -1:
			strcpy(err, "unrecognized wctl parameter");
			return -1;
		case Minx:
			r.min.x = set(sign, r.min.x-xy, xy, r.min.x+xy);
			break;
		case Miny:
			r.min.y = set(sign, r.min.y-xy, xy, r.min.y+xy);
			break;
		case Maxx:
			r.max.x = set(sign, r.max.x-xy, xy, r.max.x+xy);
			break;
		case Maxy:
			r.max.y = set(sign, r.max.y-xy, xy, r.max.y+xy);
			break;
		case Deltax:
			r.max.x = set(sign, r.max.x-xy, r.min.x+xy, r.max.x+xy);
			break;
		case Deltay:
			r.max.y = set(sign, r.max.y-xy, r.min.y+xy, r.max.y+xy);
			break;
		case Id:
			if(idp != nil)
				*idp = xy;
			break;
		case PID:
			if(pidp != nil)
				*pidp = xy;
			break;
		case Wpx:
			if( xy != 0)
				 *xscalep = 1;
			else
				*xscalep = 0;

			break;
		case Wpy:
			if( xy != 0)
				*yscalep = 1;
			else
				*yscalep = 0;
			break;
		}
	}

	*rp = rectonscreen(rectaddpt(r, screen->r.min));

	while(isspace(*s))
		s++;
	if(cmd!=New && *s!='\0'){
		strcpy(err, "extraneous text in wctl message");
		return -1;
	}

	if(argp)
		*argp = s;

	return cmd;
}

int
wctlnew(Rectangle rect, char *arg, int pid, int hideit, int scrollit, char *dir, char *err)
{
	char **argv;
	Image *i;

	if(!goodrect(rect)){
		strcpy(err, Ebadwr);
		return -1;
	}
	argv = emalloc(4*sizeof(char*));
	argv[0] = "rc";
	argv[1] = "-c";
	while(isspace(*arg))
		arg++;
	if(*arg == '\0'){
		argv[1] = "-i";
		argv[2] = nil;
	}else{
		argv[2] = arg;
		argv[3] = nil;
	}
	if(hideit)
		i = allocimage(display, rect, screen->chan, 0, DWhite);
	else
		i = allocwindow(wscreen, rect, Refbackup, DWhite);
	if(i == nil){
		strcpy(err, Ewalloc);
		return -1;
	}
	border(i, rect, Selborder, red, ZP);

	new(i, hideit, scrollit, pid, dir, "/bin/rc", argv);

	free(argv);	/* when new() returns, argv and args have been copied */
	return 1;
}

int
writewctl(Xfid *x, char *err)
{
	int cnt, cmd, j, k, id, hideit, scrollit, pid, ishid, xscale, yscale;
	Image *i;
	char *arg, *dir;
	Rectangle rect;
	Window *w;

	w = x->f->w;
	cnt = x->count;
	x->data[cnt] = '\0';
	id = 0;

	rect = rectsubpt(w->screenr, screen->r.min);
	cmd = parsewctl(&arg, rect, &rect, &pid, &id, &hideit, &scrollit, &xscale, &yscale, &dir, x->data, err);
	if(cmd < 0)
		return -1;

	if(mouse->buttons!=0 && cmd>=Top){
		strcpy(err, "action disallowed when mouse active");
		return -1;
	}

	if(id != 0){
		for(j=0; j<nwindow; j++)
			if(window[j]->id == id)
				break;
		if(j == nwindow){
			strcpy(err, "no such window id");
			return -1;
		}
		w = window[j];
		if(w->deleted || w->i==nil){
			strcpy(err, "window deleted");
			return -1;
		}
	}

	switch(cmd){
	case New:
		return wctlnew(rect, arg, pid, hideit, scrollit, dir, err);
	case Set:
		if(pid > 0)
			wsetpid(w, pid, 0);
		return 1;

		/* fall through */
	
	case Move:
		rect = Rect(rect.min.x, rect.min.y, rect.min.x+Dx(w->screenr), rect.min.y+Dy(w->screenr));
		rect = rectonscreen(rect);
		/* fall through */
	case Resize:
		if(!goodrect(rect)){
			strcpy(err, Ebadwr);
			return -1;
		}
		i = allocwindow(wscreen, rect, Refbackup, DWhite);
		if(i == nil){
			strcpy(err, Ewalloc);
			return -1;
		}
		border(i, rect, Selborder, red, ZP);
		wsendctlmesg(w, Reshaped, i->r, i);
	case Wallpaper:
		loadnewscale(xscale, yscale);
		if( dir ) {
			if( loadnewwallpaper( dir ) < 0 ) {
				strcpy(err, "image error");
				return -1;
			}
		}
		updatewallpaper();
		draw(screen, screen->r, background, nil, ZP);


		for(j = 0; j < nwindow; j++) {
			ishid = 0;
			for( k = 0; k < nhidden; k++ ) {
				if( window[j] = hidden[k] ) {
					ishid = 1;
					break;
				}
			}

			if( !ishid ) {
				i = allocwindow(wscreen, window[j]->i->r, Refbackup, DWhite);
				wsendctlmesg(window[j], Reshaped, window[j]->i->r, i);
			}
		}
		
		wsendctlmesg(w, Refresh, rect, nil);
		return 1;
	case Scroll:
		w->scrolling = 1;
		wshow(w, w->nr);
		wsendctlmesg(w, Wakeup, ZR, nil);
		return 1;
	case Noscroll:
		w->scrolling = 0;
		wsendctlmesg(w, Wakeup, ZR, nil);
		return 1;
	case Top:
		wtopme(w);
		return 1;
	case Bottom:
		wbottomme(w);
		return 1;
	case Current:
		wcurrent(w);
		return 1;
	case Hide:
		switch(whide(w)){
		case -1:
			strcpy(err, "window already hidden");
			return -1;
		case 0:
			strcpy(err, "hide failed");
			return -1;
		default:
			break;
		}
		return 1;
	case Unhide:
		for(j=0; j<nhidden; j++)
			if(hidden[j] == w)
				break;
		if(j == nhidden){
			strcpy(err, "window not hidden");
			return -1;
		}
		if(wunhide(j) == 0){
			strcpy(err, "hide failed");
			return -1;
		}
		return 1;
	case Delete:
		wsendctlmesg(w, Deleted, ZR, nil);
		return 1;
	}
	strcpy(err, "invalid wctl message");
	return -1;
}

void
wctlthread(void *v)
{
	char *buf, *arg, *dir;
	int cmd, id, pid, hideit, scrollit, xscale, yscale;
	Rectangle rect;
	char err[ERRMAX];
	Channel *c;

	c = v;

	threadsetname("WCTLTHREAD");

	for(;;){
		buf = recvp(c);
		cmd = parsewctl(&arg, ZR, &rect, &pid, &id, &hideit, &scrollit, &xscale, &yscale, &dir, buf, err);

		switch(cmd){
		case New:
			wctlnew(rect, arg, pid, hideit, scrollit, dir, err);
		}
		free(buf);
	}
}

void
wctlproc(void *v)
{
	char *buf;
	int n, eofs;
	Channel *c;

	threadsetname("WCTLPROC");
	c = v;

	eofs = 0;
	for(;;){
		buf = emalloc(messagesize);
		n = read(wctlfd, buf, messagesize-1);	/* room for \0 */
		if(n < 0)
			break;
		if(n == 0){
			if(++eofs > 20)
				break;
			continue;
		}
		eofs = 0;

		buf[n] = '\0';
		sendp(c, buf);
	}
}
