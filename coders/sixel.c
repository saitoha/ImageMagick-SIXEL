/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                     SSSSS  IIIII  X   X  EEEEE  L                           %
%                     SS       I     X X   E      L                           %
%                      SSS     I      X    EEE    L                           %
%                        SS    I     X X   E      L                           %
%                     SSSSS  IIIII  X   X  EEEEE  LLLLL                       %
%                                                                             %
%                                                                             %
%                        Read/Write DEC SIXEL Format                          %
%                                                                             %
%                              Software Design                                %
%                               Hayaki Saito                                  %
%                              September 2014                                 %
%                     Based on kmiya@culti's sixel decoder                    %
%                                                                             %
%                                                                             %
%  Copyright 1999-2014 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    http://www.imagemagick.org/script/license.php                            %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/attribute.h"
#include "magick/blob.h"
#include "magick/blob-private.h"
#include "magick/cache.h"
#include "magick/color.h"
#include "magick/color-private.h"
#include "magick/colormap.h"
#include "magick/colorspace.h"
#include "magick/colorspace-private.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/geometry.h"
#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/list.h"
#include "magick/magick.h"
#include "magick/memory_.h"
#include "magick/monitor.h"
#include "magick/monitor-private.h"
#include "magick/pixel-accessor.h"
#include "magick/pixel-private.h"
#include "magick/quantize.h"
#include "magick/quantum-private.h"
#include "magick/resize.h"
#include "magick/resource_.h"
#include "magick/splay-tree.h"
#include "magick/static.h"
#include "magick/string_.h"
#include "magick/module.h"
#include "magick/threshold.h"
#include "magick/utility.h"

#define RGB(r, g, b) (((r) << 16) + ((g) << 8) +  (b))
#define RGBA(r, g, b, a) (((a) << 24) + ((r) << 16) + ((g) << 8) +  (b))
#define PALVAL(n,a,m) (((n) * (a) + ((m) / 2)) / (m))
#define XRGB(r,g,b) RGB(PALVAL(r, 255, 100), PALVAL(g, 255, 100), PALVAL(b, 255, 100))
#define SIXEL_PALETTE_MAX 256

static int const ColTab[] = {
    XRGB(0,  0,  0),   /*  0 Black    */
    XRGB(20, 20, 80),  /*  1 Blue     */
    XRGB(80, 13, 13),  /*  2 Red      */
    XRGB(20, 80, 20),  /*  3 Green    */
    XRGB(80, 20, 80),  /*  4 Magenta  */
    XRGB(20, 80, 80),  /*  5 Cyan     */
    XRGB(80, 80, 20),  /*  6 Yellow   */
    XRGB(53, 53, 53),  /*  7 Gray 50% */
    XRGB(26, 26, 26),  /*  8 Gray 25% */
    XRGB(33, 33, 60),  /*  9 Blue*    */
    XRGB(60, 26, 26),  /* 10 Red*     */
    XRGB(33, 60, 33),  /* 11 Green*   */
    XRGB(60, 33, 60),  /* 12 Magenta* */
    XRGB(33, 60, 60),  /* 13 Cyan*    */
    XRGB(60, 60, 33),  /* 14 Yellow*  */
    XRGB(80, 80, 80),  /* 15 Gray 75% */
};


static int
HueToRGB(int n1, int n2, int hue)
{
    const int HLSMAX = 100;

    if (hue < 0) {
        hue += HLSMAX;
    }

    if (hue > HLSMAX) {
        hue -= HLSMAX;
    }

    if (hue < (HLSMAX / 6)) {
        return (n1 + (((n2 - n1) * hue + (HLSMAX / 12)) / (HLSMAX / 6)));
    }
    if (hue < (HLSMAX / 2)) {
        return (n2);
    }
    if (hue < ((HLSMAX * 2) / 3)) {
        return (n1 + (((n2 - n1) * (((HLSMAX * 2) / 3) - hue) + (HLSMAX / 12))/(HLSMAX / 6)));
    }
    return (n1);
}


static int
HLStoRGB(int hue, int lum, int sat)
{
    int R, G, B;
    int Magic1, Magic2;
    const int RGBMAX = 255;
    const int HLSMAX = 100;

    if (sat == 0) {
        R = G = B = (lum * RGBMAX) / HLSMAX;
    } else {
        if (lum <= (HLSMAX / 2)) {
            Magic2 = (lum * (HLSMAX + sat) + (HLSMAX / 2)) / HLSMAX;
        } else {
            Magic2 = lum + sat - ((lum * sat) + (HLSMAX / 2)) / HLSMAX;
        }
        Magic1 = 2 * lum - Magic2;

        R = (HueToRGB(Magic1, Magic2, hue + (HLSMAX / 3)) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
        G = (HueToRGB(Magic1, Magic2, hue) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
        B = (HueToRGB(Magic1, Magic2, hue - (HLSMAX / 3)) * RGBMAX + (HLSMAX/2)) / HLSMAX;
    }
    return RGB(R, G, B);
}


static unsigned char *
GetParam(unsigned char *p, int *param, int *len)
{
    int n;

    *len = 0;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (isdigit(*p)) {
            for (n = 0; isdigit(*p); p++) {
                n = n * 10 + (*p - '0');
            }
            if (*len < 10) {
                param[(*len)++] = n;
            }
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == ';') {
                p++;
            }
        } else if (*p == ';') {
            if (*len < 10) {
                param[(*len)++] = 0;
            }
            p++;
        } else
            break;
    }
    return p;
}

/* convert sixel data into indexed pixel bytes and palette data */
int
sixel_decode(unsigned char              /* in */  *p,         /* sixel bytes */
             unsigned char              /* out */ **pixels,   /* decoded pixels */
             int                        /* out */ *pwidth,    /* image width */
             int                        /* out */ *pheight,   /* image height */
             unsigned char              /* out */ **palette,  /* ARGB palette */
             int                        /* out */ *ncolors    /* palette size (<= 256) */)
{
    int n, i, r, g, b, sixel_vertical_mask, c;
    int posision_x, posision_y;
    int max_x, max_y;
    int attributed_pan, attributed_pad;
    int attributed_ph, attributed_pv;
    int repeat_count, color_index, max_color_index = 2, background_color_index;
    int param[10];
    unsigned char *s;
    static char pam[256];
    static char gra[256];
    int sixel_palet[SIXEL_PALETTE_MAX];
    unsigned char *imbuf, *dmbuf;
    int imsx, imsy;
    int dmsx, dmsy;
    int y;

    posision_x = posision_y = 0;
    max_x = max_y = 0;
    attributed_pan = 2;
    attributed_pad = 1;
    attributed_ph = attributed_pv = 0;
    repeat_count = 1;
    color_index = 0;
    background_color_index = 0;

    imsx = 2048;
    imsy = 2048;
    imbuf = malloc(imsx * imsy);

    if (imbuf == NULL) {
        return (-1);
    }

    for (n = 0; n < 16; n++) {
        sixel_palet[n] = ColTab[n];
    }

    /* colors 16-231 are a 6x6x6 color cube */
    for (r = 0; r < 6; r++) {
        for (g = 0; g < 6; g++) {
            for (b = 0; b < 6; b++) {
                sixel_palet[n++] = RGB(r * 51, g * 51, b * 51);
            }
        }
    }
    /* colors 232-255 are a grayscale ramp, intentionally leaving out */
    for (i = 0; i < 24; i++) {
        sixel_palet[n++] = RGB(i * 11, i * 11, i * 11);
    }

    for (; n < SIXEL_PALETTE_MAX; n++) {
        sixel_palet[n] = RGB(255, 255, 255);
    }

    memset(imbuf, background_color_index, imsx * imsy);

    pam[0] = gra[0] = '\0';

    while (*p != '\0') {
        if ((p[0] == '\033' && p[1] == 'P') || *p == 0x90) {
            if (*p == '\033') {
                p++;
            }

            s = ++p;
            p = GetParam(p, param, &n);
            if (s < p) {
                for (i = 0; i < 255 && s < p;) {
                    pam[i++] = *(s++);
                }
                pam[i] = '\0';
            }

            if (*p == 'q') {
                p++;

                if (n > 0) {        /* Pn1 */
                    switch(param[0]) {
                    case 0:
                    case 1:
                        attributed_pad = 2;
                        break;
                    case 2:
                        attributed_pad = 5;
                        break;
                    case 3:
                        attributed_pad = 4;
                        break;
                    case 4:
                        attributed_pad = 4;
                        break;
                    case 5:
                        attributed_pad = 3;
                        break;
                    case 6:
                        attributed_pad = 3;
                        break;
                    case 7:
                        attributed_pad = 2;
                        break;
                    case 8:
                        attributed_pad = 2;
                        break;
                    case 9:
                        attributed_pad = 1;
                        break;
                    }
                }

                if (n > 2) {        /* Pn3 */
                    if (param[2] == 0) {
                        param[2] = 10;
                    }
                    attributed_pan = attributed_pan * param[2] / 10;
                    attributed_pad = attributed_pad * param[2] / 10;
                    if (attributed_pan <= 0) attributed_pan = 1;
                    if (attributed_pad <= 0) attributed_pad = 1;
                }
            }

        } else if ((p[0] == '\033' && p[1] == '\\') || *p == 0x9C) {
            break;
        } else if (*p == '"') {
            /* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
            s = p++;
            p = GetParam(p, param, &n);
            if (s < p) {
                for (i = 0; i < 255 && s < p;) {
                    gra[i++] = *(s++);
                }
                gra[i] = '\0';
            }

            if (n > 0) attributed_pad = param[0];
            if (n > 1) attributed_pan = param[1];
            if (n > 2 && param[2] > 0) attributed_ph = param[2];
            if (n > 3 && param[3] > 0) attributed_pv = param[3];

            if (attributed_pan <= 0) attributed_pan = 1;
            if (attributed_pad <= 0) attributed_pad = 1;

            if (imsx < attributed_ph || imsy < attributed_pv) {
                dmsx = imsx > attributed_ph ? imsx : attributed_ph;
                dmsy = imsy > attributed_pv ? imsy : attributed_pv;
                dmbuf = malloc(dmsx * dmsy);
                if (dmbuf == NULL) {
                    return (-1);
                }
                memset(dmbuf, background_color_index, dmsx * dmsy);
                for (y = 0; y < imsy; ++y) {
                    memcpy(dmbuf + dmsx * y, imbuf + imsx * y, imsx);
                }
                free(imbuf);
                imsx = dmsx;
                imsy = dmsy;
                imbuf = dmbuf;
            }

        } else if (*p == '!') {
            /* DECGRI Graphics Repeat Introducer ! Pn Ch */
            p = GetParam(++p, param, &n);

            if (n > 0) {
                repeat_count = param[0];
            }

        } else if (*p == '#') {
            /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
            p = GetParam(++p, param, &n);

            if (n > 0) {
                if ((color_index = param[0]) < 0) {
                    color_index = 0;
                } else if (color_index >= SIXEL_PALETTE_MAX) {
                    color_index = SIXEL_PALETTE_MAX - 1;
                }
            }

            if (n > 4) {
                if (param[1] == 1) {            /* HLS */
                    if (param[2] > 360) param[2] = 360;
                    if (param[3] > 100) param[3] = 100;
                    if (param[4] > 100) param[4] = 100;
                    sixel_palet[color_index] = HLStoRGB(param[2] * 100 / 360, param[3], param[4]);
                } else if (param[1] == 2) {    /* RGB */
                    if (param[2] > 100) param[2] = 100;
                    if (param[3] > 100) param[3] = 100;
                    if (param[4] > 100) param[4] = 100;
                    sixel_palet[color_index] = XRGB(param[2], param[3], param[4]);
                }
            }

        } else if (*p == '$') {
            /* DECGCR Graphics Carriage Return */
            p++;
            posision_x = 0;
            repeat_count = 1;

        } else if (*p == '-') {
            /* DECGNL Graphics Next Line */
            p++;
            posision_x  = 0;
            posision_y += 6;
            repeat_count = 1;

        } else if (*p >= '?' && *p <= '\177') {
            if (imsx < (posision_x + repeat_count) || imsy < (posision_y + 6)) {
                int nx = imsx * 2;
                int ny = imsy * 2;

                while (nx < (posision_x + repeat_count) || ny < (posision_y + 6)) {
                    nx *= 2;
                    ny *= 2;
                }

                dmsx = nx;
                dmsy = ny;
                if ((dmbuf = malloc(dmsx * dmsy)) == NULL) {
                    return (-1);
                }
                memset(dmbuf, background_color_index, dmsx * dmsy);
                for (y = 0; y < imsy; ++y) {
                    memcpy(dmbuf + dmsx * y, imbuf + imsx * y, imsx);
                }
                free(imbuf);
                imsx = dmsx;
                imsy = dmsy;
                imbuf = dmbuf;
            }

            if (color_index > max_color_index) {
                max_color_index = color_index;
            }
            if ((b = *(p++) - '?') == 0) {
                posision_x += repeat_count;

            } else {
                sixel_vertical_mask = 0x01;

                if (repeat_count <= 1) {
                    for (i = 0; i < 6; i++) {
                        if ((b & sixel_vertical_mask) != 0) {
                            imbuf[imsx * (posision_y + i) + posision_x] = color_index;
                            if (max_x < posision_x) {
                                max_x = posision_x;
                            }
                            if (max_y < (posision_y + i)) {
                                max_y = posision_y + i;
                            }
                        }
                        sixel_vertical_mask <<= 1;
                    }
                    posision_x += 1;

                } else { /* repeat_count > 1 */
                    for (i = 0; i < 6; i++) {
                        if ((b & sixel_vertical_mask) != 0) {
                            c = sixel_vertical_mask << 1;
                            for (n = 1; (i + n) < 6; n++) {
                                if ((b & c) == 0) {
                                    break;
                                }
                                c <<= 1;
                            }
                            for (y = posision_y + i; y < posision_y + i + n; ++y) {
                                memset(imbuf + imsx * y + posision_x, color_index, repeat_count);
                            }
                            if (max_x < (posision_x + repeat_count - 1)) {
                                max_x = posision_x + repeat_count - 1;
                            }
                            if (max_y < (posision_y + i + n - 1)) {
                                max_y = posision_y + i + n - 1;
                            }

                            i += (n - 1);
                            sixel_vertical_mask <<= (n - 1);
                        }
                        sixel_vertical_mask <<= 1;
                    }
                    posision_x += repeat_count;
                }
            }
            repeat_count = 1;
        } else {
            p++;
        }
    }

    if (++max_x < attributed_ph) {
        max_x = attributed_ph;
    }
    if (++max_y < attributed_pv) {
        max_y = attributed_pv;
    }

    if (imsx > max_x || imsy > max_y) {
        dmsx = max_x;
        dmsy = max_y;
        if ((dmbuf = malloc(dmsx * dmsy)) == NULL) {
            return (-1);
        }
        for (y = 0; y < dmsy; ++y) {
            memcpy(dmbuf + dmsx * y, imbuf + imsx * y, dmsx);
        }
        free(imbuf);
        imsx = dmsx;
        imsy = dmsy;
        imbuf = dmbuf;
    }

    *pixels = imbuf;
    *pwidth = imsx;
    *pheight = imsy;
    *ncolors = max_color_index + 1;
    *palette = malloc(*ncolors * 4);
    for (n = 0; n < *ncolors; ++n) {
        (*palette)[n * 4 + 0] = sixel_palet[n] >> 16 & 0xff;
        (*palette)[n * 4 + 1] = sixel_palet[n] >> 8 & 0xff;
        (*palette)[n * 4 + 2] = sixel_palet[n] & 0xff;
        (*palette)[n * 4 + 3] = 0xff;
    }
    return 0;
}



/*
  Forward declarations.
*/
static MagickBooleanType
  WriteSIXELImage(const ImageInfo *,Image *);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I s S I X E L                                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsSIXEL() returns MagickTrue if the image format type, identified by the
%  magick string, is SIXEL.
%
%  The format of the IsSIXEL method is:
%
%      MagickBooleanType IsSIXEL(const unsigned char *magick,const size_t length)
%
%  A description of each parameter follows:
%
%    o magick: compare image format pattern against these bytes. or
%      blob.
%
%    o length: Specifies the length of the magick string.
%
*/
static MagickBooleanType IsSIXEL(const unsigned char *magick,const size_t length)
{
  if (length < 9)
    return(MagickFalse);
  if (LocaleNCompare((char *) magick,"\033P",2) == 0)
    return(MagickTrue);
  return(MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e a d S I X E L I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadSIXELImage() reads an X11 pixmap image file and returns it.  It
%  allocates the memory necessary for the new Image structure and returns a
%  pointer to the new image.
%
%  The format of the ReadSIXELImage method is:
%
%      Image *ReadSIXELImage(const ImageInfo *image_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static Image *ReadSIXELImage(const ImageInfo *image_info,ExceptionInfo *exception)
{
  char
    key[MaxTextExtent],
    target[MaxTextExtent],
    *sixel_buffer;

  Image
    *image;

  MagickBooleanType
    active,
    status;

  register char
    *p,
    *q,
    *next;

  register IndexPacket
    *indexes;

  register ssize_t
    x;

  register PixelPacket
    *r;

  size_t
    length;

  SplayTreeInfo
    *sixel_colors;

  ssize_t
    count,
    i,
    j,
    y;

  unsigned long
    colors,
    columns,
    rows,
    width;

  unsigned char *sixel_pixels, *sixel_palette;
  int sixel_width, sixel_height, sixel_ncolrs;

  /*
    Open image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickSignature);
  if (image_info->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      image_info->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickSignature);
  image=AcquireImage(image_info);
  status=OpenBlob(image_info,image,ReadBinaryBlobMode,exception);
  if (status == MagickFalse)
    {
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  /*
    Read SIXEL file.
  */
  length=MaxTextExtent;
  sixel_buffer=(char *) AcquireQuantumMemory((size_t) length,sizeof(*sixel_buffer));
  p=sixel_buffer;
  if (sixel_buffer != (char *) NULL)
    while (ReadBlobString(image,p) != (char *) NULL)
    {
      if ((*p == '#') && ((p == sixel_buffer) || (*(p-1) == '\n')))
        continue;
      if ((*p == '}') && (*(p+1) == ';'))
        break;
      p+=strlen(p);
      if ((size_t) (p-sixel_buffer+MaxTextExtent) < length)
        continue;
      length<<=1;
      sixel_buffer=(char *) ResizeQuantumMemory(sixel_buffer,length+MaxTextExtent,
        sizeof(*sixel_buffer));
      if (sixel_buffer == (char *) NULL)
        break;
      p=sixel_buffer+strlen(sixel_buffer);
    }
  if (sixel_buffer == (char *) NULL)
    ThrowReaderException(ResourceLimitError,"MemoryAllocationFailed");

  /*
    Decode SIXEL
  */
  if (sixel_decode(sixel_buffer, &sixel_pixels, &image->columns, &image->rows, &sixel_palette, &image->colors) != 0)
    ThrowReaderException(CorruptImageError,"CorruptImage");
  image->depth=24;
  image->storage_class=PseudoClass;

  if (AcquireImageColormap(image,image->colors) == MagickFalse)
    ThrowReaderException(ResourceLimitError,"MemoryAllocationFailed");
  for (i = 0; i < image->colors; ++i) {
    image->colormap[i].red   =(MagickRealType) ScaleCharToQuantum(sixel_palette[i * 4 + 0]);
    image->colormap[i].green =(MagickRealType) ScaleCharToQuantum(sixel_palette[i * 4 + 1]);
    image->colormap[i].blue  =(MagickRealType) ScaleCharToQuantum(sixel_palette[i * 4 + 2]);
  }

  j=0;
  if (image_info->ping == MagickFalse)
    {
      /*
        Read image pixels.
      */
      for (y=0; y < (ssize_t) image->rows; y++)
      {
        r=QueueAuthenticPixels(image,0,y,image->columns,1,exception);
        if (r == (PixelPacket *) NULL)
          break;
        indexes=GetAuthenticIndexQueue(image);
        for (x=0; x < (ssize_t) image->columns; x++)
        {
          j=(ssize_t) sixel_pixels[y * image->columns + x];
          SetPixelIndex(indexes+x,j);
          r++;
          p+=width;
        }
        if (SyncAuthenticPixels(image,exception) == MagickFalse)
          break;
      }
      if (y < (ssize_t) image->rows)
        ThrowReaderException(CorruptImageError,"NotEnoughPixelData");
    }
  /*
    Relinquish resources.
  */
  (void) CloseBlob(image);
  return(GetFirstImageInList(image));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e g i s t e r S I X E L I m a g e                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RegisterSIXELImage() adds attributes for the SIXEL image format to
%  the list of supported formats.  The attributes include the image format
%  tag, a method to read and/or write the format, whether the format
%  supports the saving of more than one frame to the same file or blob,
%  whether the format supports native in-memory I/O, and a brief
%  description of the format.
%
%  The format of the RegisterSIXELImage method is:
%
%      size_t RegisterSIXELImage(void)
%
*/
ModuleExport size_t RegisterSIXELImage(void)
{
  MagickInfo
    *entry;

  entry=SetMagickInfo("SIXEL");
  entry->decoder=(DecodeImageHandler *) ReadSIXELImage;
  entry->encoder=(EncodeImageHandler *) WriteSIXELImage;
  entry->magick=(IsImageFormatHandler *) IsSIXEL;
  entry->adjoin=MagickFalse;
  entry->description=ConstantString("DEC SIXEL Graphics Format");
  entry->module=ConstantString("SIXEL");
  (void) RegisterMagickInfo(entry);
  return(MagickImageCoderSignature);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   U n r e g i s t e r S I X E L I m a g e                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  UnregisterSIXELImage() removes format registrations made by the
%  SIXEL module from the list of supported formats.
%
%  The format of the UnregisterSIXELImage method is:
%
%      UnregisterSIXELImage(void)
%
*/
ModuleExport void UnregisterSIXELImage(void)
{
  (void) UnregisterMagickInfo("SIXEL");
  (void) UnregisterMagickInfo("SIX");
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   W r i t e S I X E L I m a g e                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WriteSIXELImage() writes an image to a file in the X pixmap format.
%
%  The format of the WriteSIXELImage method is:
%
%      MagickBooleanType WriteSIXELImage(const ImageInfo *image_info,
%        Image *image,ExceptionInfo *exception)
%
%  A description of each parameter follows.
%
%    o image_info: the image info.
%
%    o image:  The image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType WriteSIXELImage(const ImageInfo *image_info,Image *image)
{
  char
    buffer[MaxTextExtent],
    basename[MaxTextExtent],
    name[MaxTextExtent],
    symbol[MaxTextExtent];

  ExceptionInfo
    *exception;

  MagickBooleanType
    status;

  MagickPixelPacket
    pixel;

  register const IndexPacket
    *indexes;

  register const PixelPacket
    *p;

  register ssize_t
    i,
    x;

  ssize_t
    j,
    k,
    opacity,
    y;

  /*
    Open output image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickSignature);
  assert(image != (Image *) NULL);
  assert(image->signature == MagickSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  exception=(&image->exception);
  status=OpenBlob(image_info,image,WriteBinaryBlobMode,exception);
  if (status == MagickFalse)
    return(status);
  if (IssRGBCompatibleColorspace(image->colorspace) == MagickFalse)
    (void) TransformImageColorspace(image,sRGBColorspace);
  opacity=(-1);
  if (image->matte == MagickFalse)
    {
      if ((image->storage_class == DirectClass) || (image->colors > 256))
        (void) SetImageType(image,PaletteType);
    }
  else
    {
      MagickRealType
        alpha,
        beta;

      /*
        Identify transparent colormap index.
      */
      if ((image->storage_class == DirectClass) || (image->colors > 256))
        (void) SetImageType(image,PaletteBilevelMatteType);
      for (i=0; i < (ssize_t) image->colors; i++)
        if (image->colormap[i].opacity != OpaqueOpacity)
          {
            if (opacity < 0)
              {
                opacity=i;
                continue;
              }
            alpha=(Quantum) TransparentOpacity-(MagickRealType)
              image->colormap[i].opacity;
            beta=(Quantum) TransparentOpacity-(MagickRealType)
              image->colormap[opacity].opacity;
            if (alpha < beta)
              opacity=i;
          }
      if (opacity == -1)
        {
          (void) SetImageType(image,PaletteBilevelMatteType);
          for (i=0; i < (ssize_t) image->colors; i++)
            if (image->colormap[i].opacity != OpaqueOpacity)
              {
                if (opacity < 0)
                  {
                    opacity=i;
                    continue;
                  }
                alpha=(Quantum) TransparentOpacity-(MagickRealType)
                  image->colormap[i].opacity;
                beta=(Quantum) TransparentOpacity-(MagickRealType)
                  image->colormap[opacity].opacity;
                if (alpha < beta)
                  opacity=i;
              }
        }
      if (opacity >= 0)
        {
          image->colormap[opacity].red=image->transparent_color.red;
          image->colormap[opacity].green=image->transparent_color.green;
          image->colormap[opacity].blue=image->transparent_color.blue;
        }
    }
  /*
    SIXEL header.
  */
  (void) WriteBlobString(image,"\033P" "0;0;0q\n");
  GetPathComponent(image->filename,BasePath,basename);
  if (isalnum((int) ((unsigned char) *basename)) == 0)
    {
      (void) FormatLocaleString(buffer,MaxTextExtent,"sixel_%s",basename);
      (void) CopyMagickString(basename,buffer,MaxTextExtent);
    }
  (void) FormatLocaleString(buffer,MaxTextExtent,
    "\"1;1;%d %d\n", image->columns, image->rows);
  (void) WriteBlobString(image,buffer);
  for (i=0; i < (ssize_t) image->colors; i++)
  {
    /*
      Define SIXEL color.
    */
    (void) FormatLocaleString(buffer,MaxTextExtent,
                              "#%d;2;%d;%d;%d\n",i,
                              (int)image->colormap[i].red * 100 / 0xffff,
                              (int)image->colormap[i].green * 100 / 0xffff,
                              (int)image->colormap[i].blue * 100 / 0xffff);
    (void) WriteBlobString(image,buffer);
  }
  /*
    Define SIXEL pixels.
  */
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    p=GetVirtualPixels(image,0,y,image->columns,1,exception);
    if (p == (const PixelPacket *) NULL)
      break;
    indexes=GetVirtualIndexQueue(image);
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      k=((ssize_t) GetPixelIndex(indexes+x));
      (void) FormatLocaleString(buffer,MaxTextExtent,"#%d%c",k,0x3f + (1 << y % 6));
      (void) WriteBlobString(image,buffer);
      p++;
    }
    (void) FormatLocaleString(buffer,MaxTextExtent,"%s\n",
      (y == (ssize_t) (image->rows-1) ? "" : (y % 6 == 5 ? "-": "$")));
    (void) WriteBlobString(image,buffer);
  }
  (void) WriteBlobString(image,"\033\\");

  (void) CloseBlob(image);
  return(MagickTrue);
}
