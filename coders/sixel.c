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

static int const sixel_default_color_table[] = {
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
hue_to_rgb(int n1, int n2, int hue)
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
hls_to_rgb(int hue, int lum, int sat)
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

        R = (hue_to_rgb(Magic1, Magic2, hue + (HLSMAX / 3)) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
        G = (hue_to_rgb(Magic1, Magic2, hue) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
        B = (hue_to_rgb(Magic1, Magic2, hue - (HLSMAX / 3)) * RGBMAX + (HLSMAX/2)) / HLSMAX;
    }
    return RGB(R, G, B);
}


static unsigned char *
get_params(unsigned char *p, int *param, int *len)
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
             size_t                     /* out */ *pwidth,    /* image width */
             size_t                     /* out */ *pheight,   /* image height */
             unsigned char              /* out */ **palette,  /* ARGB palette */
             unsigned long              /* out */ *ncolors    /* palette size (<= 256) */)
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
    imbuf = AcquireQuantumMemory(imsx * imsy,1);

    if (imbuf == NULL) {
        return (-1);
    }

    for (n = 0; n < 16; n++) {
        sixel_palet[n] = sixel_default_color_table[n];
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
            p = get_params(p, param, &n);
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
            p = get_params(p, param, &n);
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
                dmbuf = AcquireQuantumMemory(dmsx * dmsy,1);
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
            p = get_params(++p, param, &n);

            if (n > 0) {
                repeat_count = param[0];
            }

        } else if (*p == '#') {
            /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
            p = get_params(++p, param, &n);

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
                    sixel_palet[color_index] = hls_to_rgb(param[2] * 100 / 360, param[3], param[4]);
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
                if ((dmbuf = AcquireQuantumMemory(dmsx * dmsy,1)) == NULL) {
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
        if ((dmbuf = AcquireQuantumMemory(dmsx * dmsy,1)) == NULL) {
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
    *palette = AcquireQuantumMemory(*ncolors,4);
    for (n = 0; n < *ncolors; ++n) {
        (*palette)[n * 4 + 0] = sixel_palet[n] >> 16 & 0xff;
        (*palette)[n * 4 + 1] = sixel_palet[n] >> 8 & 0xff;
        (*palette)[n * 4 + 2] = sixel_palet[n] & 0xff;
        (*palette)[n * 4 + 3] = 0xff;
    }
    return 0;
}

#define SIXEL_OUTPUT_PACKET_SIZE 1024

typedef struct sixel_node {
    struct sixel_node *next;
    int pal;
    int sx;
    int mx;
    unsigned char *map;
} sixel_node_t;

typedef int (* sixel_write_function)(char *data, int size, void *priv);

typedef struct sixel_output {

    int ref;

    /* compatiblity flags */

    /* 0: 7bit terminal,
     * 1: 8bit terminal */
    unsigned char has_8bit_control;

    /* 0: the terminal has sixel scrolling
     * 1: the terminal does not have sixel scrolling */
    unsigned char has_sixel_scrolling;

    /* 0: DECSDM set (CSI ? 80 h) enables sixel scrolling
       1: DECSDM set (CSI ? 80 h) disables sixel scrolling */
    unsigned char has_sdm_glitch;

    sixel_write_function fn_write;

    unsigned char conv_palette[256];
    int save_pixel;
    int save_count;
    int active_palette;

    sixel_node_t *node_top;
    sixel_node_t *node_free;

    void *priv;
    int pos;
    unsigned char buffer[1];

} sixel_output_t;

int sixel_write(char *data, int size, void *priv)
{
    return WriteBlob((Image *)priv,size,(unsigned char*)data);
}

sixel_output_t * const
sixel_output_create(sixel_write_function fn_write, void *priv)
{
    sixel_output_t *output;

    output = AcquireQuantumMemory(sizeof(sixel_output_t) + SIXEL_OUTPUT_PACKET_SIZE * 2, 1);
    output->ref = 1;
    output->has_8bit_control = 0;
    output->has_sdm_glitch = 0;
    output->fn_write = fn_write;
    output->save_pixel = 0;
    output->save_count = 0;
    output->active_palette = (-1);
    output->node_top = NULL;
    output->node_free = NULL;
    output->priv = priv;
    output->pos = 0;

    return output;
}

static void
sixel_advance(sixel_output_t *context, int nwrite)
{
    if ((context->pos += nwrite) >= SIXEL_OUTPUT_PACKET_SIZE) {
        context->fn_write((char *)context->buffer, SIXEL_OUTPUT_PACKET_SIZE, context->priv);
        memcpy(context->buffer,
               context->buffer + SIXEL_OUTPUT_PACKET_SIZE,
               (context->pos -= SIXEL_OUTPUT_PACKET_SIZE));
    }
}


static int
sixel_put_flash(sixel_output_t *const context)
{
    int n;
    int nwrite;

#if defined(USE_VT240)        /* VT240 Max 255 ? */
    while (context->save_count > 255) {
        nwrite = spritf((char *)context->buffer + context->pos, "!255%c", context->save_pixel);
        if (nwrite <= 0) {
            return (-1);
        }
        sixel_advance(context, nwrite);
        context->save_count -= 255;
    }
#endif  /* defined(USE_VT240) */

    if (context->save_count > 3) {
        /* DECGRI Graphics Repeat Introducer ! Pn Ch */
        nwrite = sprintf((char *)context->buffer + context->pos, "!%d%c", context->save_count, context->save_pixel);
        if (nwrite <= 0) {
            return (-1);
        }
        sixel_advance(context, nwrite);
    } else {
        for (n = 0; n < context->save_count; n++) {
            context->buffer[context->pos] = (char)context->save_pixel;
            sixel_advance(context, 1);
        }
    }

    context->save_pixel = 0;
    context->save_count = 0;

    return 0;
}


static void
sixel_put_pixel(sixel_output_t *const context, int pix)
{
    if (pix < 0 || pix > '?') {
        pix = 0;
    }

    pix += '?';

    if (pix == context->save_pixel) {
        context->save_count++;
    } else {
        sixel_put_flash(context);
        context->save_pixel = pix;
        context->save_count = 1;
    }
}


static void
sixel_node_del(sixel_output_t *const context, sixel_node_t *np)
{
    sixel_node_t *tp;

    if ((tp = context->node_top) == np) {
        context->node_top = np->next;
    }

    else {
        while (tp->next != NULL) {
            if (tp->next == np) {
                tp->next = np->next;
                break;
            }
            tp = tp->next;
        }
    }

    np->next = context->node_free;
    context->node_free = np;
}


static int
sixel_put_node(sixel_output_t *const context, int x,
        sixel_node_t *np, int ncolors, int keycolor)
{
    int nwrite;

    if (ncolors != 2 || keycolor == -1) {
        /* designate palette index */
        if (context->active_palette != np->pal) {
            nwrite = sprintf((char *)context->buffer + context->pos,
                             "#%d", context->conv_palette[np->pal]);
            sixel_advance(context, nwrite);
            context->active_palette = np->pal;
        }
    }

    for (; x < np->sx; x++) {
        if (x != keycolor) {
            sixel_put_pixel(context, 0);
        }
    }

    for (; x < np->mx; x++) {
        if (x != keycolor) {
            sixel_put_pixel(context, np->map[x]);
        }
    }

    sixel_put_flash(context);

    return x;
}


static int
sixel_encode_impl(unsigned char *pixels, int width, int height,
                  unsigned char *palette, int ncolors, int keycolor,
                  sixel_output_t *context)
{
    int x, y, i, n, c;
    int sx, mx;
    int len, pix;
    unsigned char *map;
    sixel_node_t *np, *tp, top;
    unsigned char list[SIXEL_PALETTE_MAX];
    int nwrite;

    context->pos = 0;

    if (ncolors < 1) {
        return (-1);
    }
    len = ncolors * width;
    context->active_palette = (-1);

    if ((map = (unsigned char *)AcquireQuantumMemory(len, sizeof(unsigned char))) == NULL) {
        return (-1);
    }
    memset(map, 0, len);
    for (n = 0; n < ncolors; n++) {
        context->conv_palette[n] = list[n] = n;
    }

    if (context->has_8bit_control) {
        nwrite = sprintf((char *)context->buffer, "\x90" "0;0;0" "q");
    } else {
        nwrite = sprintf((char *)context->buffer, "\x1bP" "0;0;0" "q");
    }
    if (nwrite <= 0) {
        return (-1);
    }
    sixel_advance(context, nwrite);
    nwrite = sprintf((char *)context->buffer + context->pos, "\"1;1;%d;%d", width, height);
    if (nwrite <= 0) {
        return (-1);
    }
    sixel_advance(context, nwrite);

    if (ncolors != 2 || keycolor == -1) {
        for (n = 0; n < ncolors; n++) {
            /* DECGCI Graphics Color Introducer  # Pc ; Pu; Px; Py; Pz */
            nwrite = sprintf((char *)context->buffer + context->pos, "#%d;2;%d;%d;%d",
                             context->conv_palette[n],
                             (palette[n * 3 + 0] * 100 + 127) / 255,
                             (palette[n * 3 + 1] * 100 + 127) / 255,
                             (palette[n * 3 + 2] * 100 + 127) / 255);
            if (nwrite <= 0) {
                return (-1);
            }
            sixel_advance(context, nwrite);
            if (nwrite <= 0) {
                return (-1);
            }
        }
        context->buffer[context->pos] = '\n';
        sixel_advance(context, 1);
    }

    for (y = i = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            pix = pixels[y * width + x];
            if (pix >= 0 && pix < ncolors && pix != keycolor) {
                map[pix * width + x] |= (1 << i);
            }
        }

        if (++i < 6 && (y + 1) < height) {
            continue;
        }

        for (c = 0; c < ncolors; c++) {
            for (sx = 0; sx < width; sx++) {
                if (*(map + c * width + sx) == 0) {
                    continue;
                }

                for (mx = sx + 1; mx < width; mx++) {
                    if (*(map + c * width + mx) != 0) {
                        continue;
                    }

                    for (n = 1; (mx + n) < width; n++) {
                        if (*(map + c * width + mx + n) != 0) {
                            break;
                        }
                    }

                    if (n >= 10 || (mx + n) >= width) {
                        break;
                    }
                    mx = mx + n - 1;
                }

                if ((np = context->node_free) != NULL) {
                    context->node_free = np->next;
                } else if ((np = (sixel_node_t *)malloc(sizeof(sixel_node_t))) == NULL) {
                    return (-1);
                }

                np->pal = c;
                np->sx = sx;
                np->mx = mx;
                np->map = map + c * width;

                top.next = context->node_top;
                tp = &top;

                while (tp->next != NULL) {
                    if (np->sx < tp->next->sx) {
                        break;
                    } else if (np->sx == tp->next->sx && np->mx > tp->next->mx) {
                        break;
                    }
                    tp = tp->next;
                }

                np->next = tp->next;
                tp->next = np;
                context->node_top = top.next;

                sx = mx - 1;
            }

        }

        for (x = 0; (np = context->node_top) != NULL;) {
            if (x > np->sx) {
                /* DECGCR Graphics Carriage Return */
                context->buffer[context->pos] = '$';
                sixel_advance(context, 1);
                x = 0;
            }

            x = sixel_put_node(context, x, np, ncolors, keycolor);
            sixel_node_del(context, np);
            np = context->node_top;

            while (np != NULL) {
                if (np->sx < x) {
                    np = np->next;
                    continue;
                }

                x = sixel_put_node(context, x, np, ncolors, keycolor);
                sixel_node_del(context, np);
                np = context->node_top;
            }
        }

        /* DECGNL Graphics Next Line */
        context->buffer[context->pos] = '-';
        sixel_advance(context, 1);
        if (nwrite <= 0) {
            return (-1);
        }

        i = 0;
        memset(map, 0, len);
    }

    if (context->has_8bit_control) {
        context->buffer[context->pos] = '\x9c';
        sixel_advance(context, 1);
    } else {
        context->buffer[context->pos] = '\x1b';
        context->buffer[context->pos + 1] = '\\';
        sixel_advance(context, 2);
    }
    if (nwrite <= 0) {
        return (-1);
    }

    /* flush buffer */
    if (context->pos > 0) {
        context->fn_write((char *)context->buffer, context->pos, context->priv);
    }

    /* free nodes */
    while ((np = context->node_free) != NULL) {
        context->node_free = np->next;
        free(np);
    }

    free(map);

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
  const unsigned char
    *end = magick + length;

  if (length < 3)
    return(MagickFalse);

  if (*magick == 0x90 || (*magick == 0x1b && *++magick == 'P')) {
    while (++magick != end) {
      if (*magick == 'q')
        return(MagickTrue);
      if (!(*magick >= '0' && *magick <= '9') && *magick != ';')
        return(MagickFalse);
    }
  }
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
    *sixel_buffer;

  Image
    *image;

  MagickBooleanType
    status;

  register char
    *p;

  register IndexPacket
    *indexes;

  register ssize_t
    x;

  register PixelPacket
    *r;

  size_t
    length;

  ssize_t
    i,
    j,
    y;

  unsigned char *sixel_pixels, *sixel_palette;

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
  if (sixel_decode((unsigned char *)sixel_buffer, &sixel_pixels, &image->columns, &image->rows, &sixel_palette, &image->colors) != 0)
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
  ExceptionInfo
    *exception;

  MagickBooleanType
    status;

  register const IndexPacket
    *indexes;

  register const PixelPacket
    *p;

  register ssize_t
    i,
    x;

  ssize_t
    opacity,
    y;

  sixel_output_t
    *output;

  int
    ret;

  unsigned char
      sixel_palette[256 * 3],
      *sixel_pixels;

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
  for (i=0; i < (ssize_t) image->colors; i++)
  {
    sixel_palette[i * 3 + 0] = image->colormap[i].red   / 0x100;
    sixel_palette[i * 3 + 1] = image->colormap[i].green / 0x100;
    sixel_palette[i * 3 + 2] = image->colormap[i].blue  / 0x100;
  }

  /*
    Define SIXEL pixels.
  */
  output = sixel_output_create(sixel_write, (void *)image);
  sixel_pixels =(unsigned char *) AcquireQuantumMemory((size_t) image->columns * image->rows,1);
  p=GetVirtualPixels(image,0,0,image->columns,image->rows,exception);
  indexes=GetVirtualIndexQueue(image);
  for (y=0; y < (ssize_t) image->rows; y++)
    for (x=0; x < (ssize_t) image->columns; x++)
      sixel_pixels[y * image->columns + x]
          = ((ssize_t) GetPixelIndex(indexes + y * image->columns + x));
  ret = sixel_encode_impl(sixel_pixels, image->columns, image->rows,
                          sixel_palette, image->colors, -1,
                          output);
  (void) CloseBlob(image);
  return(MagickTrue);
}
