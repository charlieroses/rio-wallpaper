
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

void loadnewscale( int xscale, int yscale ) {
	if( xscale >= 0 )
		wpconfig->scalex = xscale;
	if( yscale >= 0 )
		wpconfig->scaley = yscale;
}

int loadnewwallpaper(char *fname) {
	int fd, l;

	fd = open(fname, OREAD);

	if( fd < 0 )
		return -1;
	close(fd);

	l = 0;
	while( fname[l] != '\0' )
		l++;

	if( l+1 > wpconfig->fnlen ) {
		wpconfig->fnlen = l +1;
		wpconfig->filename = realloc(wpconfig->filename, l+1);	
	}
	strcpy(wpconfig->filename, fname);
	return 1;
}

int updatewallpaper(void) {
	// Implementing background support from
	// https://wiki.xxiivv.com/site/rio.html
	// Make sure wallpaper image is plan9 format
	// $ jpg -9t wallpaper.jpg > wallpaper
	int fd;
	Image *i;
	Display *d;

	fd = open(wpconfig->filename, OREAD);

	if( fd < 0 )
		return -1;

	d = background->display;
	i = readimage(d, fd, 0);
	close(fd);

	if( i ) {
		resizmple(i, Dx(screen->r), Dy(screen->r));
		freeimage(background);
		background = allocimage(d, Rect(0,0,Dx(i->r), Dy(i->r)), RGB24, 1, 0x777777FF);
		draw(background, background->r, i, 0, i->r.min);
		return 1;
	}

	return -1;

}

void resizmple(Image *img, int xn, int yn) { 
	int xo, yo;		// old x and y
	int xu, yu;		// scale up x and y? 1 for bigger, 0 for smaller
	int xd, yd;		// The magnitude of scale of x and y
	int xc , yc;		// used to center x and y if necessary
	int xno, yno;	// the new old (yeah i know im not good at variable names)
				// in the cases where the image is being centered, and the
				// x and y of the image aren't the screen size (xn , yn) or the
				// old image size (xo, yo), then the image is sized (xno, yno).
				// (xn, yn) = (2*xc + xno,  2*yc + yno)
	int bpp;		// Bytes per pixel
	uchar *io;
	uchar *in;
	int c, r, i, ro, co, po, pn;

	xo = Dx(img->r);
	yo = Dy(img->r);
	bpp = img->depth >> 3;

	io = malloc(xo * yo * bpp);
	unloadimage(img, (img->r), io, (xo*yo*bpp));
	in = malloc(xn * yn * bpp);


	// Decide how this image is going to get resized
	xno = 0;
	yno = 0;
	// what are the differences between the screen size
	// and image size
	// are we getting bigger or smaller? by how much?
	if( xo > xn ) {
		xu = 0;
		xd = xo / xn;
	}
	else {
		xu = 1;
		xd = xn / xo;
	}

	if( yo > yn ) {
		yu = 0;
		yd = yo / yn;
	}
	else {
		yu = 1;
		yd = yn / yo;
	}

	// what does the user want?
	if( (wpconfig->scalex == 0) && (wpconfig->scaley == 0) ) {
		xno = xo;
		yno = yo;
		xd = 0;
		yd = 0;
	}
	if( (wpconfig->scalex == 0) && (wpconfig->scaley == 1) ) {
		// here, update x scale values as per y scale values
		yno = yn;
		xu = yu;
		xd = yd;

		if( xu == 1)
			xno = xo*xd;
		else
			xno = xo/xd;
	}
	if( (wpconfig->scalex == 1) && (wpconfig->scaley == 0) ) {
		// here, update y scale values as per x scale values
		xno = xn;
		yu = xu;
		yd = xd;

		if( yu == 1 )
			yno = yo*yd;
		else
			yno = yo/yd;

	}
	if( (wpconfig->scalex == 1) && (wpconfig->scaley == 1) ) {
		xno = xn;
		yno = yn;
	}

	// does the image need to be centered?
	if( xno < xn )
		xc = (xn - xno) / 2;
	else
		xc = 0;
	if( yno < yn )
		yc = (yn - yno) / 2;
	else
		yc = 0;
	

	// Now actually resize the images
	ro = 0;
	for( r = 0; r < yn; r++ ) {
		co = 0;
		for( c = 0; c < xn; c++ ) {
			pn = ((r * xn) + c) * bpp;
			po = ((ro *xo) + co) * bpp;

			if( (r <= yc) || ((yn-yc) <= r) || (c <= xc) || ((xn-xc) <= c) ) {
				for( i = 0; i < bpp; i++ ) 
					in[pn+i] = 0x77;
				continue;
			}

			for( i = 0; i < bpp; i++ ) 
				in[pn+i] = io[po+i];

			if( xu == 1) {
				// Bigger Width
				if ( ((c-xc) * xo) > (co * xno) )
					co++;
			}
			else {
				if ( ((c-xc) * xo) < (co * xno) )
					co++;
				co += xd;
			}
			if( co > xo )
				co = xo;
		}

		if( (yc < r) && (r < (yn-yc)) ) {
			if(yu == 1) {
				// Bigger Height
				if( ((r -yc) * yo) > (ro * yno) )
					ro++;
			}
			else {
				// Smaller Height
				if( ((r-yc) * yo) < (ro * yno) )
					ro++;
				ro += yd;
			}
			if( ro > yo )
				ro = yo;
		}
	}

	reallocimage(img, xn, yn, in, (xn*yn*bpp));

	free(in);
	free(io);
}

void reallocimage(Image *i, int x, int y, uchar *data, int ndata) {
	Display *d;
	Rectangle r;
	ulong c;

	d = i->display;
	c = i->chan;
	r = Rect(i->r.min.x, i->r.min.y, i->r.min.x+x, i->r.min.y+y);

	freeimage(i);
	i = allocimage(d, r, c, 0, -1);
	loadimage(i, i->r, data, ndata);
}

WPConfig *allocwpconfig(int x, int y) {
	WPConfig *c;

	c = malloc(sizeof(WPConfig));
	c->scalex = x;
	c->scaley = y;
	c->fnlen = 32;
	c->filename = malloc(c->fnlen);

	return c;
}