#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <vlc_common.h>
#include <vlc_cpu.h>
#include <vlc_picture.h>
#include "bench_asm.h"
#include "../../modules/video_filter/deinterlace/deinterlace.h"

static ordered_renderer_t (*ordered_renderer)(unsigned pxsize);
static single_pic_renderer_t (*single_pic_renderer)(unsigned pxsize);
static ordered_renderer_t render_ordered;
static single_pic_renderer_t render_single_pic;
static picture_t *srcpic;
static picture_t *dstpic;
static int pixel_size;

static inline void
setup_iovfmt(video_format_t *ifmt, video_format_t *ofmt,
             bool half_height, int bpc)
{
    vlc_fourcc_t chroma = bpc == 8 ? VLC_CODEC_I420 : VLC_CODEC_I420_10L;
    video_format_Setup(ifmt, chroma, 640, 480, 640, 480, 4, 3);
    *ofmt = *ifmt;
    if (half_height)
    {
        ofmt->i_height /= 2;
        ofmt->i_visible_height /= 2;
        ofmt->i_y_offset /= 2;
        ofmt->i_sar_den /= 2;
    }
}

static int
init_deinterlacer(bool half_height, int bpc)
{
    video_format_t in_fmt, out_fmt;
    setup_iovfmt(&in_fmt, &out_fmt, half_height, bpc);
    srcpic = picture_NewFromFormat(&in_fmt);
    if (srcpic == NULL) goto error;
    dstpic = picture_NewFromFormat(&out_fmt);
    if (dstpic == NULL) goto error;
    pixel_size = bpc / 8;
    return VLC_SUCCESS;
error:
    if (srcpic)
        picture_Release(srcpic);
    return VLC_EGENERIC;
}

#define INIT_DEINTERLACER(type, mode, Mode, half_height, bpc)               \
static int                                                                  \
init_deinterlacer_##mode##_##bpc##bit(void)                                 \
{                                                                           \
    type##_renderer = Mode##Renderer;                                       \
    return init_deinterlacer(half_height, bpc);                             \
}

INIT_DEINTERLACER(ordered, linear, Linear, false, 8)
INIT_DEINTERLACER(ordered, linear, Linear, false, 16)
INIT_DEINTERLACER(single_pic, mean, Mean, true, 8)
INIT_DEINTERLACER(single_pic, mean, Mean, true, 16)
INIT_DEINTERLACER(single_pic, blend, Blend, false, 8)
INIT_DEINTERLACER(single_pic, blend, Blend, false, 16)

static void
destroy_deinterlacer(void)
{
    picture_Release(srcpic);
    picture_Release(dstpic);
}

#define CHECK_FEATURE_DEINTERLACER(type)                                    \
static int                                                                  \
check_feature_deinterlacer_##type(int flag)                                 \
{                                                                           \
    render_##type = type##_renderer(pixel_size);                            \
    assert(render_##type);                                                  \
    if (flag == 0)                                                          \
        return VLC_SUCCESS;                                                 \
    vlc_CPU_mask(flag);                                                     \
    int has_impl = render_##type != type##_renderer(pixel_size);            \
    vlc_CPU_unmask(flag);                                                   \
    return has_impl ? VLC_SUCCESS : VLC_EGENERIC;                           \
}

CHECK_FEATURE_DEINTERLACER(ordered)
CHECK_FEATURE_DEINTERLACER(single_pic)

#define BENCH_DEINTERLACER(type, ...)                                       \
static uint64_t                                                             \
bench_deinterlacer_##type(void)                                             \
{                                                                           \
    uint64_t cycles = 0;                                                    \
    for (int i = 0; i < 4096; ++i)                                          \
    {                                                                       \
        uint64_t const cycles_start = read_cycle_counter();                 \
        render_##type(__VA_ARGS__);                                         \
        uint64_t const cycles_end = read_cycle_counter();                   \
        cycles += cycles_end - cycles_start;                                \
    }                                                                       \
    return (cycles + (1UL << 11)) >> 12;                                    \
}

BENCH_DEINTERLACER(ordered, NULL, dstpic, srcpic, 0, i & 1)
BENCH_DEINTERLACER(single_pic, NULL, dstpic, srcpic)

#define SUBSCRIBE_DEINTERLACER(type, mode, bpc)                             \
void                                                                        \
subscribe_deinterlacer_##mode##_##bpc##bit(int id)                          \
{                                                                           \
    bench_asm_subscribe(id, #mode " deinterlacer " #bpc "-bit",             \
                        init_deinterlacer_##mode##_##bpc##bit,              \
                        destroy_deinterlacer,                               \
                        check_feature_deinterlacer_##type,                  \
                        bench_deinterlacer_##type, true);                   \
}

SUBSCRIBE_DEINTERLACER(ordered, linear, 8)
SUBSCRIBE_DEINTERLACER(ordered, linear, 16)
SUBSCRIBE_DEINTERLACER(single_pic, mean, 8)
SUBSCRIBE_DEINTERLACER(single_pic, mean, 16)
SUBSCRIBE_DEINTERLACER(single_pic, blend, 8)
SUBSCRIBE_DEINTERLACER(single_pic, blend, 16)
