#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include <vlc_plugin.h>

#define str(x)  #x
#define FILTER_PREFIX   "fgseg-"

#define K_NAME      FILTER_PREFIX "K"
#define K_DEFAULT   3
#define K_TEXT      N_("Number of surfaces per pixel (3-7)")
#define K_LONGTEXT  N_("Set the number of K surfaces per pixel view, " \
                       "between 3 and 7. Defaults to " str(K_DEFAULT) ".")

#define MF_NAME     FILTER_PREFIX "match-flex"
#define MF_DEFAULT  0.5
#define MF_TEXT     N_("Surface matching flexibility (0-1)")
#define MF_LONGTEXT N_("Set the surface matching flexibility, " \
                       "between 0 and 1. Defaults to " str(MF_DEFAULT) ".")

#define THRES_NAME      FILTER_PREFIX "bg-thres"
#define THRES_DEFAULT   0.7
#define THRES_TEXT      N_("Background intensity threshold (0-1)")
#define THRES_LONGTEXT  N_("Set the threshold value of the cumulated " \
                           "probability of surfaces for background " \
                           "substraction, between 0 and 1. " \
                           "Defaults to " str(THRES_DEFAULT) ".")

/*
 *  everything is described in
 *  http://www.cse.psu.edu/~rtc12/CSE586Spring2010/papers/emBGsubtractAboutSandG.pdf
 */

static inline void greyscale(picture_t *pic)
{
    memset(pic->U_PIXELS, 127, pic->p[U_PLANE].i_lines * pic->U_PITCH);
    memset(pic->V_PIXELS, 127, pic->p[V_PLANE].i_lines * pic->V_PITCH);
}

static inline float pow2f(float const x)
{
    return x * x;
}

struct theta
{
    float   mu;
    float   var;
};

struct phi
{
    float           *omega;
    struct theta    *theta;
};

struct filter_sys_t
{
    struct phi  *p_gmm;
    int         k;
    float       alpha;
    float       lambda_squared;
    float       bg_likelihood;
    bool        b_first;
};

static inline void normalize_omega(float omega[], int kmax)
{
    float cum_pk = .0f;
    for (int k = 0; k < kmax; ++k)
        cum_pk += omega[k];
    for (int k = 0; k < kmax; ++k)
        omega[k] /= cum_pk;
}

static inline bool match(uint8_t x, struct theta *theta, float lamdba_squared)
{
    return pow2f(x - theta->mu) <= lamdba_squared * theta->var;
}

static inline void rank_surfaces(int surf[], int kmax, struct phi *gmm)
{
    float scores[kmax];
    for (int k = 0; k < kmax; ++k)
        scores[k] = pow2f(gmm->omega[k]) / gmm->theta[k].var;

    for (int k = 0; k < kmax; ++k)
    {
        int max = 0;
        for (int i = 0; i < kmax; ++i)
            if (scores[i] > scores[max])
                max = i;
        scores[max] = .0f;
        surf[k] = max;
    }
}

static inline bool is_background(float bg_likelihood,
                                 float pk[], bool k_match[], int sorted[],
                                 int kmax, bool debug)
{
    float cum_pk = .0f;
    for (int k = 0; k < kmax && cum_pk < bg_likelihood; ++k)
    {
        cum_pk += pk[sorted[k]];
        if (k_match[sorted[k]])
        {
            if (debug)
                fprintf(stderr, "matching bg state %d\n", sorted[k]);
            return true;
        }
    }
    if (debug)
        fprintf(stderr, "matching fg state\n");
    return false;
}

static void Filter_pix(uint8_t *p_x,
                       struct phi *gmm, int kmax,
                       float alpha, float lambda_squared, float bg_likelihood, bool debug)
{
    uint8_t const x = *p_x;
    bool b_match = false;
    bool k_match[kmax];

    if (debug)
        fprintf(stderr, "-------------- %d  ------------\n", x);

    for (int k = 0; k < kmax; ++k)
    {
        if (debug)
            fprintf(stderr, "%f * %f\n", gmm->omega[k], 1.f - alpha);
        gmm->omega[k] *= 1.f - alpha;
        if (debug)
            fprintf(stderr, "%f\n", gmm->omega[k]);

        k_match[k] = match(x, gmm->theta + k, lambda_squared);
        if (k_match[k])
        {
            b_match = true;

            gmm->omega[k] += alpha;
            alpha /= gmm->omega[k];     //  /!\ costy

            float mu = gmm->theta[k].mu;
            gmm->theta[k].mu = (1.f - alpha) * mu + alpha * x;

            float var0 = gmm->theta[k].var;
            float var1 = pow2f(x - mu);
            gmm->theta[k].var = (1.f - alpha) * var0 + alpha * var1;
        }
    }
    normalize_omega(gmm->omega, kmax);

    int sorted[kmax];
    rank_surfaces(sorted, kmax, gmm);

    if (!b_match)
    {
        if (debug)
            fprintf(stderr, "new state k=%d\n", sorted[kmax - 1]);
        int min_k = sorted[kmax - 1];
//        gmm->omega[min_k] = .005f;
        gmm->theta[min_k].mu = x;
        gmm->theta[min_k].var = pow2f(30.f);
//        normalize_omega(gmm->omega, kmax);
    }

    if (is_background(bg_likelihood, gmm->omega, k_match, sorted, kmax, debug))
        *p_x = 0;

    if (debug)
    {
        fprintf(stderr, "\n");
        for (int k = 0; k < kmax; ++k)
        {
            fprintf(stderr, "[%d].omega = %f\n", k, gmm->omega[k]);
            fprintf(stderr, "[%d].mu = %f\n", k, gmm->theta[k].mu);
            fprintf(stderr, "[%d].var = %f\n", k, gmm->theta[k].var);
            fprintf(stderr, "\n");
        }
    }

    if (debug)
        fprintf(stderr, "------------------------------\n\n");
}

static picture_t *Filter(filter_t *filter, picture_t *ipic)
{
    picture_t *opic = filter_NewPicture(filter);
    if (!opic)
        return NULL;
    picture_Copy(opic, ipic);
    picture_Release(ipic);

//    greyscale(opic);

    filter_sys_t *p_sys = filter->p_sys;
    int w = filter->fmt_in.video.i_visible_width;

    for (int i = 0; i < opic->p[Y_PLANE].i_visible_lines; ++i)
        for (int j = 0; j < opic->p[Y_PLANE].i_visible_pitch; ++j)
        {
            uint8_t *p_pix = opic->Y_PIXELS + i * opic->Y_PITCH + j;
            struct phi *gmm = p_sys->p_gmm + i * w + j;

            int n = 5;
loop:
            Filter_pix(p_pix, gmm, p_sys->k,
                       p_sys->alpha, p_sys->lambda_squared, p_sys->bg_likelihood, (i == 200 && j == 200) == 0x2A);
            if (p_sys->b_first && n--)
                goto loop;
        }
    if (p_sys->b_first)
        p_sys->b_first = false;

    for (int i = 0; i < opic->p[Y_PLANE].i_visible_lines; i += 2)
        for (int j = 0; j < opic->p[Y_PLANE].i_visible_pitch; j += 2)
            if (!opic->Y_PIXELS[(i + 0) * opic->Y_PITCH + j + 0] &&
                !opic->Y_PIXELS[(i + 0) * opic->Y_PITCH + j + 1] &&
                !opic->Y_PIXELS[(i + 1) * opic->Y_PITCH + j + 0] &&
                !opic->Y_PIXELS[(i + 1) * opic->Y_PITCH + j + 1])
            {
                opic->U_PIXELS[i * opic->U_PITCH / 2 + j / 2] = 127;
                opic->V_PIXELS[i * opic->V_PITCH / 2 + j / 2] = 127;
            }

    return opic;
}

static int init_internal(filter_t *filter)
{
    int k = var_CreateGetIntegerCommand(filter, K_NAME);
    float flex = var_CreateGetFloatCommand(filter, MF_NAME);
    float thres = var_CreateGetFloatCommand(filter, THRES_NAME);

    video_format_t *vfmt = &filter->fmt_in.video;
    uint32_t num_pix = vfmt->i_visible_width * vfmt->i_visible_height;
    size_t filter_sys_sz = sizeof(filter_sys_t) +
                           num_pix * sizeof(struct phi) +
                           num_pix * k * sizeof(float) +
                           num_pix * k * sizeof(struct theta);

    filter_sys_t *p_sys = malloc(filter_sys_sz);
    if (!p_sys)
        return VLC_ENOMEM;
    filter->p_sys = p_sys;

    p_sys->p_gmm = (struct phi *)(p_sys + 1);
    p_sys->k = k;
    p_sys->alpha = .001f;
    p_sys->lambda_squared = pow2f(2.f + flex);
    p_sys->bg_likelihood = thres;
    p_sys->b_first = true;

    float *p_omega = (float *)(p_sys->p_gmm + num_pix);
    struct theta *p_theta = (struct theta *)(p_omega + num_pix * k);
    for (size_t i = 0; i < num_pix; ++i)
    {
        p_sys->p_gmm[i].omega = p_omega + i * k;
        p_sys->p_gmm[i].theta = p_theta + i * k;
    }
    for (size_t i = 0; i < num_pix * k; ++i)
    {
        p_omega[i] = 1.f / k;
        p_theta[i].mu = rand() % 256;
        p_theta[i].var = pow2f(30.f);
    }

    return VLC_SUCCESS;
}

static int Open(vlc_object_t *p_obj)
{
    filter_t *p_filter = (filter_t *)p_obj;

    vlc_fourcc_t fourcc = p_filter->fmt_in.video.i_chroma;
    vlc_chroma_description_t const *p_chroma = vlc_fourcc_GetChromaDescription(fourcc);
    if (p_chroma == NULL || p_chroma->plane_count != 3 || p_chroma->pixel_size != 1)
    {
        msg_Err(p_obj, "Unsupported chroma (%4.4s)", (char *)&fourcc);
        return VLC_EGENERIC;
    }
    msg_Info(p_obj, "Chroma (%4.4s)", (char *)&fourcc);

    p_filter->pf_video_filter = Filter;
    return init_internal(p_filter);
}

static void Close(vlc_object_t *p_obj)
{
    free(((filter_t *)p_obj)->p_sys);
}

#define FGSEG_HELP      N_("Seperate foreground from static background")

vlc_module_begin()
    set_description(N_("Foreground segmentation video filter"))
    set_shortname(N_("Foreground segmentation"))
    set_help(FGSEG_HELP)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("video filter", 0)
    add_integer(K_NAME, K_DEFAULT, K_TEXT, K_LONGTEXT, false)
    add_float(MF_NAME, MF_DEFAULT, MF_TEXT, MF_LONGTEXT, false)
    add_float(THRES_NAME, THRES_DEFAULT, THRES_TEXT, THRES_LONGTEXT, false)
    add_shortcut("fgseg")
    set_callbacks(Open, Close)
vlc_module_end()
