/*============================================================================
 * BlueXLogger - tools/gen_ico.c
 * Generates the product icon (multi-size, 32bpp BGRA ICO) at build time so
 * the repository carries no binary blobs.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_IMAGES 6

typedef struct {
    int      size;
    unsigned char *xor_bits;   /* bottom-up BGRA */
    unsigned char *and_bits;
    unsigned      xor_len;
    unsigned      and_len;
} IconImage;

/*----------------------------------------------------------------------------
 * Rendering: rounded square with a vertical blue gradient and an "X" mark.
 *--------------------------------------------------------------------------*/
static void render_bgra(unsigned char *top_down, int size)
{
    int x, y;

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            unsigned char *p = top_down + ((size_t)y * size + x) * 4;
            double fx = (x + 0.5) / size;
            double fy = (y + 0.5) / size;

            /* Background gradient: deep navy -> blue. */
            double r = 14.0 + 12.0 * fy;
            double g = 20.0 + 34.0 * fy;
            double b = 32.0 + 74.0 * fy;

            /* The X: two diagonals, softened towards the centre. */
            {
                double d1 = fabs(fx - fy);
                double d2 = fabs(fx - (1.0 - fy));
                double d  = (d1 < d2) ? d1 : d2;
                double w  = 0.155;
                if (d < w) {
                    double e = 1.0 - (d / w);
                    e = e * e;
                    r += (96.0  - r) * e;
                    g += (176.0 - g) * e;
                    b += (255.0 - b) * e;
                }
            }

            /* Rounded-square alpha mask with a one-pixel feather. */
            {
                double cx = fabs(fx - 0.5);
                double cy = fabs(fy - 0.5);
                double lim = 0.5 - 0.115;
                double d = 0.0;
                double alpha = 1.0;

                if (cx > lim && cy > lim) {
                    double dx = cx - lim, dy = cy - lim;
                    d = sqrt(dx * dx + dy * dy);
                }
                if (d > 0.115) alpha = 0.0;
                else if (d > 0.115 - (1.2 / size))
                    alpha = (0.115 - d) * size / 1.2;

                if (alpha <= 0.0) {
                    p[0] = p[1] = p[2] = p[3] = 0;
                    continue;
                }
                if (alpha > 1.0) alpha = 1.0;

                p[0] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
                p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
                p[2] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
                p[3] = (unsigned char)(alpha * 255.0 + 0.5);
            }
        }
    }
}

static int build_image(IconImage *img, int size)
{
    unsigned char *top_down;
    int y;
    unsigned mask_stride = (unsigned)(((size + 31) / 32) * 4);

    top_down = (unsigned char *)calloc((size_t)size * size * 4, 1);
    if (!top_down) return 0;

    render_bgra(top_down, size);

    img->size     = size;
    img->xor_len  = (unsigned)(size * size * 4);
    img->and_len  = mask_stride * (unsigned)size;
    img->xor_bits = (unsigned char *)malloc(img->xor_len);
    img->and_bits = (unsigned char *)calloc(img->and_len, 1);

    if (!img->xor_bits || !img->and_bits) {
        free(top_down);
        return 0;
    }

    /* ICO stores the XOR bitmap bottom-up. */
    for (y = 0; y < size; y++) {
        memcpy(img->xor_bits + (size_t)y * size * 4,
               top_down + (size_t)(size - 1 - y) * size * 4,
               (size_t)size * 4);
    }

    free(top_down);
    return 1;
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

int main(int argc, char **argv)
{
    static const int sizes[] = { 16, 32, 48, 256 };
    IconImage images[MAX_IMAGES];
    int  count = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int  i;
    FILE *f;
    unsigned char hdr[6];
    unsigned offset;

    if (argc < 2) {
        fprintf(stderr, "usage: gen_ico <output.ico>\n");
        return 1;
    }

    memset(images, 0, sizeof(images));

    for (i = 0; i < count; i++) {
        if (!build_image(&images[i], sizes[i])) {
            fprintf(stderr, "gen_ico: failed to render %d px\n", sizes[i]);
            return 1;
        }
    }

    f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "gen_ico: cannot open %s\n", argv[1]);
        return 1;
    }

    hdr[0] = 0; hdr[1] = 0;                    /* reserved */
    put16(hdr + 2, 1);                         /* type: icon */
    put16(hdr + 4, (unsigned)count);
    fwrite(hdr, 1, 6, f);

    offset = 6 + 16 * (unsigned)count;

    for (i = 0; i < count; i++) {
        unsigned char e[16];
        unsigned total = 40 + images[i].xor_len + images[i].and_len;

        e[0] = (unsigned char)(images[i].size >= 256 ? 0 : images[i].size);
        e[1] = (unsigned char)(images[i].size >= 256 ? 0 : images[i].size);
        e[2] = 0;                              /* palette */
        e[3] = 0;                              /* reserved */
        put16(e + 4, 1);                       /* planes */
        put16(e + 6, 32);                      /* bpp */
        put32(e + 8, total);
        put32(e + 12, offset);
        fwrite(e, 1, 16, f);

        offset += total;
    }

    for (i = 0; i < count; i++) {
        unsigned char bih[40];
        memset(bih, 0, sizeof(bih));
        put32(bih + 0, 40);
        put32(bih + 4, (unsigned)images[i].size);
        put32(bih + 8, (unsigned)(images[i].size * 2));  /* XOR + AND */
        put16(bih + 12, 1);
        put16(bih + 14, 32);
        put32(bih + 20, images[i].xor_len);
        fwrite(bih, 1, 40, f);
        fwrite(images[i].xor_bits, 1, images[i].xor_len, f);
        fwrite(images[i].and_bits, 1, images[i].and_len, f);
    }

    fclose(f);

    for (i = 0; i < count; i++) {
        free(images[i].xor_bits);
        free(images[i].and_bits);
    }

    printf("gen_ico: wrote %s (%d sizes)\n", argv[1], count);
    return 0;
}
