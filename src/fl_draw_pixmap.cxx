//
// "$Id$"
//
// Pixmap drawing code for the Fast Light Tool Kit (FLTK).
//
// Copyright 1998-2018 by Bill Spitzak and others.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     http://www.fltk.org/COPYING.php
//
// Please report all bugs and problems on the following page:
//
//     http://www.fltk.org/str.php
//

// NOTE: I believe many of the following comments (between the dash markers)
// are no longer accurate:
// --------------------------------------------------------------------
// Implemented without using the xpm library (which I can't use because
// it interferes with the color cube used by fl_draw_image).
// Current implementation is cheap and slow, and works best on a full-color
// display.  Transparency is not handled, and colors are dithered to
// the color cube.  Color index is achieved by adding the id
// characters together!  Also mallocs a lot of temporary memory!
// Notice that there is no pixmap file interface.  This is on purpose,
// as I want to discourage programs that require support files to work.
// All data needed by a program ui should be compiled in!!!
// --------------------------------------------------------------------
// The above comments were checked in as r2, and much has changed since then;
// transparency added, color cube not required, etc.      -erco Oct 20 2013

#include "config_lib.h"
#include <FL/Fl.H>
#include <FL/Fl_System_Driver.H>
#if defined(FL_CFG_SYS_WIN32)
#include "drivers/GDI/Fl_GDI_Graphics_Driver.H"
#endif
#include <FL/platform.H>
#include <FL/fl_draw.H>
#include <stdio.h>
#include "flstring.h"


static int ncolors, chars_per_pixel;

typedef struct { uchar r; uchar g; uchar b; } UsedColor;
static UsedColor *used_colors;
static int color_count; 	    // # of non-transparent colors used in pixmap

/**
  Get the dimensions of a pixmap.
  An XPM image contains the dimensions in its data. This function
  returns the width and height.
  \param[in]  data pointer to XPM image data.
  \param[out] w,h  width and height of image
  \returns non-zero if the dimensions were parsed OK
  \returns 0 if there were any problems
  */
int fl_measure_pixmap(/*const*/ char* const* data, int &w, int &h) {
  return fl_measure_pixmap((const char*const*)data,w,h);
}

/**
  Get the dimensions of a pixmap.
  \see fl_measure_pixmap(char* const* data, int &w, int &h)
  */
int fl_measure_pixmap(const char * const *cdata, int &w, int &h) {
  int i = sscanf(cdata[0],"%d%d%d%d",&w,&h,&ncolors,&chars_per_pixel);
  if (i<4 || w<=0 || h<=0 ||
      (chars_per_pixel!=1 && chars_per_pixel!=2) ) return w=0;
  return 1;
}


/**
  Draw XPM image data, with the top-left corner at the given position.
  The image is dithered on 8-bit displays so you won't lose color
  space for programs displaying both images and pixmaps.
  \param[in] data pointer to XPM image data
  \param[in] x,y  position of top-left corner
  \param[in] bg   background color
  \returns 0 if there was any error decoding the XPM data.
  */
int fl_draw_pixmap(/*const*/ char* const* data, int x,int y,Fl_Color bg) {
  return fl_draw_pixmap((const char*const*)data,x,y,bg);
}


#if defined(FL_CFG_SYS_WIN32)


// Makes an RGB triplet different from all the colors used in the pixmap
// and compute Fl_Graphics_Driver::need_pixmap_bg_color from this triplet
void Fl_GDI_Graphics_Driver::make_unused_color_(uchar &r, uchar &g, uchar &b) {
  int i;
  r = 2; g = 3; b = 4;
  while (1) {
    for ( i=0; i<color_count; i++ )
      if ( used_colors[i].r == r &&
           used_colors[i].g == g &&
           used_colors[i].b == b )
	break;
    if (i >= color_count) {
      free((void*)used_colors); used_colors = NULL;
      need_pixmap_bg_color = RGB(r, g, b);
      return;
    }
    if (r < 255) {
      r++;
    } else {
      r = 0;
      if (g < 255) {
        g++;
      } else {
	g = 0;
	b++;
      }
    }
  }
}
#endif // FL_CFG_SYS_WIN32


int fl_convert_pixmap(const char*const* cdata, uchar* out, Fl_Color bg) {
  int w, h;
  const uchar*const* data = (const uchar*const*)(cdata+1);
  uchar *transparent_c = (uchar *)0; // such that transparent_c[0,1,2] are the RGB of the transparent color
  
  if (!fl_measure_pixmap(cdata, w, h))
    return 0;
  
  if ((chars_per_pixel < 1) || (chars_per_pixel > 2))
    return 0;
  
  typedef uchar uchar4[4];
  uchar4 *colors = new uchar4[1<<(chars_per_pixel*8)];
  
  if (Fl_Graphics_Driver::need_pixmap_bg_color) {
    color_count = 0;
    used_colors = (UsedColor*)malloc(abs(ncolors) * sizeof(UsedColor));
  }
  
  if (ncolors < 0) {	// FLTK (non standard) compressed colormap
    ncolors = -ncolors;
    const uchar *p = *data++;
    // if first color is ' ' it is transparent (put it later to make
    // it not be transparent):
    if (*p == ' ') {
      uchar* c = colors[(int)' '];
      Fl::get_color(bg, c[0], c[1], c[2]); c[3] = 0;
      if (Fl_Graphics_Driver::need_pixmap_bg_color) transparent_c = c;
      p += 4;
      ncolors--;
    }
    // read all the rest of the colors:
    for (int i=0; i < ncolors; i++) {
      uchar* c = colors[*p++];
      if (Fl_Graphics_Driver::need_pixmap_bg_color) {
        used_colors[color_count].r = *(p+0);
        used_colors[color_count].g = *(p+1);
        used_colors[color_count].b = *(p+2);
        color_count++;
      }
      *c++ = *p++;
      *c++ = *p++;
      *c++ = *p++;
      *c = 255;
    }
  } else {	// normal XPM colormap with names
    for (int i=0; i<ncolors; i++) {
      const uchar *p = *data++;
      // the first 1 or 2 characters are the color index:
      int ind = *p++;
      uchar* c;
      if (chars_per_pixel>1)
        ind = (ind<<8)|*p++;
      c = colors[ind];
      // look for "c word", or last word if none:
      const uchar *previous_word = p;
      for (;;) {
        while (*p && isspace(*p)) p++;
        uchar what = *p++;
        while (*p && !isspace(*p)) p++;
        while (*p && isspace(*p)) p++;
        if (!*p) {p = previous_word; break;}
        if (what == 'c') break;
        previous_word = p;
        while (*p && !isspace(*p)) p++;
      }
      int parse = fl_parse_color((const char*)p, c[0], c[1], c[2]);
      c[3] = 255;
      if (parse) {
        if (Fl_Graphics_Driver::need_pixmap_bg_color) {
          used_colors[color_count].r = c[0];
          used_colors[color_count].g = c[1];
          used_colors[color_count].b = c[2];
          color_count++;
        }
      } else {
        // assume "None" or "#transparent" for any errors
        // "bg" should be transparent...
        Fl::get_color(bg, c[0], c[1], c[2]);
        c[3] = 255;
        if (Fl_Graphics_Driver::need_pixmap_bg_color) transparent_c = c;
      } // if parse
    } // for ncolors
  } // if ncolors
  if (Fl_Graphics_Driver::need_pixmap_bg_color) {
    if (transparent_c) {
      fl_graphics_driver->make_unused_color_(transparent_c[0], transparent_c[1], transparent_c[2]);
    } else {
      uchar r, g, b;
      fl_graphics_driver->make_unused_color_(r, g, b);
    }
  }
  
  U32 *q = (U32*)out;
  for (int Y = 0; Y < h; Y++) {
    const uchar* p = data[Y];
    if (chars_per_pixel <= 1) {
      for (int X = 0; X < w; X++)
        memcpy(q++, colors[*p++], 4);
    } else {
      for (int X = 0; X < w; X++) {
        int ind = (*p++)<<8;
        ind |= *p++;
        memcpy(q++, colors[ind], 4);
      }
    }
  }
  delete[] colors;
  return 1;
}

/**
  Draw XPM image data, with the top-left corner at the given position.
  \see fl_draw_pixmap(char* const* data, int x, int y, Fl_Color bg)
  */
int fl_draw_pixmap(const char*const* cdata, int x, int y, Fl_Color bg) {
  int w, h;

  if (!fl_measure_pixmap(cdata, w, h))
    return 0;

  uchar *buffer = new uchar[w*h*4];

  if (!fl_convert_pixmap(cdata, buffer, bg)) {
    delete[] buffer;
    return 0;
  }

  // build the mask bitmap used by Fl_Pixmap:
  uchar **p = fl_graphics_driver->mask_bitmap();
  if (p && *p) {
    int W = (w+7)/8;
    uchar* bitmap = new uchar[W * h];
    *p = bitmap;
    const uchar *p = &buffer[3];
    uchar b = 0;
    for (int Y = 0; Y < h; Y++) {
      b = 0;
      for (int X = 0, bit = 1; X < w; X++, p += 4) {
        if (*p > 127)
          b |= bit;
	    bit <<= 1;
        if (bit > 0x80 || X == w-1) {
	    *bitmap++ = b;
          bit = 1;
	    b = 0;
	  }
      } // if chars_per_pixel
    } // for Y
  }

  fl_draw_image(buffer, x, y, w, h, 4);

  delete[] buffer;
  return 1;
}

//
// End of "$Id$".
//
