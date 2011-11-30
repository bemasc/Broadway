#include <stdio.h>
#include <stdlib.h>
#include <SDL/SDL.h>

#include "yuv2rgb.h"

#include "SDL/SDL.h"

#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "nestegg/nestegg.h"

#define DEBUG_LOGGING 1
#if DEBUG_LOGGING
  #define DLOG printf
#else
  #define DLOG(...)
#endif

#define RENDER 1

#define FASTYUV2RGB 0

// Globals

SDL_Surface* screen = NULL;
uint8_t* stream = NULL;
uint8_t* buffer = NULL;
vpx_codec_ctx_t          decoder;
vpx_codec_dec_cfg_t     cfg = {0};

#define VP8_FOURCC (0x00385056)
static const struct
{
    char const *name;
    const vpx_codec_iface_t *iface;
    unsigned int             fourcc;
    unsigned int             fourcc_mask;
} ifaces[] =
{
    {"vp8",  &vpx_codec_vp8_dx_algo,   VP8_FOURCC, 0x00FFFFFF},
};
#include "vpxdec_helpers.c"
struct input_ctx        input = {.kind=RAW_FILE,
                                 .infile=NULL,
                                 .nestegg_ctx=NULL,
                                 .pkt=NULL,
                                 .chunk=0,
                                 .chunks=0,
                                 .video_track=0};
int size=0;


// Main loop handling

enum mainLoopStatus {
  MLS_STOP = 0,
  MLS_CONTINUE = 1,
  MLS_FRAMERENDERED = 2
};

// Runs the main loop. This is replaced in JavaScript with an asynchronous loop
// that calls mainLoopIteration
void runMainLoop();
enum mainLoopStatus mainLoopIteration();

#ifdef LINUX
int main(int argc, char **argv) {
#else
int SDL_main(int argc, char **argv) {
#endif

    FILE                  *infile;
    const char * fn = argc == 2 ? argv[1] : "../Media/big-buck-bunny_trailer.webm";
    unsigned int           fourcc;
    unsigned int            width;
    unsigned int            height;
    unsigned int            fps_den;
    unsigned int            fps_num;

    infile = fopen(fn, "rb");
    if (!infile)
    {
        fprintf(stderr, "Failed to open file '%s'\n", fn);
        return EXIT_FAILURE;
    }
    else
    {
        fprintf(stderr, "Successfully opened file '%s'\n", fn);
    }

    input.infile = infile;
    if(file_is_ivf(infile, &fourcc, &width, &height, &fps_den,
                   &fps_num))
        input.kind = IVF_FILE;
    else if(file_is_webm(&input, &fourcc, &width, &height, &fps_den, &fps_num))
        input.kind = WEBM_FILE;
    else if(file_is_raw(infile, &fourcc, &width, &height, &fps_den, &fps_num))
        input.kind = RAW_FILE;
    else
    {
        fprintf(stderr, "Unrecognized input file type.\n");
        return EXIT_FAILURE;
    }

    if (vpx_codec_dec_init(&decoder, ifaces[0].iface, &cfg, 0))
    {
        fprintf(stderr, "Failed to initialize decoder: %s\n", vpx_codec_error(&decoder));
        return EXIT_FAILURE;
    }

#if RENDER
    SDL_Init(SDL_INIT_VIDEO);
#endif

    runMainLoop();

    return 0;
}

void runMainLoop() {
    enum mainLoopStatus status;
    while ((status = mainLoopIteration()) != MLS_STOP);
}

extern float getPosition() {
    int offset = stream - buffer;
    return (float)offset / (float)size;
}

extern void setPosition(float value) {
    if (value < 0 || value > 1) {
        return;
    }

    int offset = (int)((float)size * value);
    stream = buffer + offset;
}

extern void paint(uint8_t *luma, uint8_t *cb, uint8_t *cr, int height, int stride) {
    int chromaStride = stride >> 1;
#if FASTYUV2RGB
    uint8_t *dst = (uint8_t *)screen->pixels;
    yuv420_2_rgb8888( (uint8_t*) dst, luma, cb, cr, width, height, width, chromaWidth, width << 2, yuv2rgb565_table, 0);
#else
    uint32_t *dst = (uint32_t *)screen->pixels;
    for (int y = 0; y < height; y++) {
        int lineOffLuma = y * stride;
        int lineOffDst = y * screen->w;
        int lineOffChroma = (y >> 1) * chromaStride;
        for (int x = 0; x < screen->w; x++) {
            int c = luma[lineOffLuma + x] - 16;
            int d = cb[lineOffChroma + (x >> 1)] - 128;
            int e = cr[lineOffChroma + (x >> 1)] - 128;

            int red = (298 * c + 409 * e + 128) >> 8;
            red = red < 0 ? 0 : (red > 255 ? 255 : red);
            int green = (298 * c - 100 * d - 208 * e + 128) >> 8;
            green = green < 0 ? 0 : (green > 255 ? 255 : green);
            int blue = (298 * c + 516 * d + 128) >> 8;
            blue = blue < 0 ? 0 : (blue > 255 ? 255 : blue);
            dst[lineOffDst + x] = SDL_MapRGB(screen->format, red & 0xff, green & 0xff, blue & 0xff);
        }
    }
#endif
}

enum mainLoopStatus mainLoopIteration() {

    enum mainLoopStatus status = MLS_CONTINUE;


    uint8_t               *buf = NULL;
    size_t                 buf_sz = 0, buf_alloc_sz = 0;
    if (!read_frame(&input, &buf, &buf_sz, &buf_alloc_sz))
    {
        vpx_codec_iter_t  iter = NULL;
        vpx_image_t    *img;
        if (vpx_codec_decode(&decoder, buf, buf_sz, NULL, 0))
        {
            const char *detail = vpx_codec_error_detail(&decoder);
            fprintf(stderr, "Failed to decode frame: %s\n", vpx_codec_error(&decoder));

            if (detail)
                fprintf(stderr, "  Additional information: %s\n", detail);
        }

        if ((img = vpx_codec_get_frame(&decoder, &iter)))
        {
#if RENDER
            if (!screen) {
                screen = SDL_SetVideoMode(img->d_w, img->d_h, 32, SDL_HWSURFACE | SDL_RESIZABLE);
            }
            SDL_LockSurface(screen);
#else
            if (!screen) {
                screen = (SDL_Surface*)malloc(sizeof(SDL_Surface));
                screen->pixels = malloc(img->stride[VPX_PLANE_Y] * img->d_h * 32);
            }
#endif
            paint(img->planes[VPX_PLANE_Y],
                  img->planes[VPX_PLANE_U], 
                  img->planes[VPX_PLANE_V],
                  img->d_h, img->stride[VPX_PLANE_Y]);
#if RENDER
            SDL_UnlockSurface(screen);
            SDL_Flip(screen);
#else
            printf("painted a frame\n");
#endif
        }

        status = MLS_FRAMERENDERED;
    } else
    {
#if RENDER
        SDL_Quit();
#endif
        return MLS_STOP;
    }
#if RENDER
#if !JS
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                exit(0);
                break;
#if !LINUX
            case SDL_KEYDOWN:
                // printf("Key: %s\n", SDL_GetKeyName( event.key.keysym.sym ));
                // printf("Key: %d\n", event.key.keysym.scancode);
                if (event.key.keysym.scancode == 1) {
                    setPosition(0.5f);
                    return status;
                } else {
                    exit(0);
                }
                break;
#endif
            }
        }
#endif
#endif

    return status;
}
