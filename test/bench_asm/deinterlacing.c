#include <assert.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <vlc/vlc.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include "bench_asm.h"

static libvlc_instance_t *libvlc;
static int shm_id;
static void *shm_addr;

static void
vlc_kill(void *arg)
{
    VLC_UNUSED(arg);
    kill(getpid(), SIGTERM);
}

static int
init_deinterlacer(char const *mode)
{
    system("cp lib_test_bench_asm_deint_plugin.la ../modules");
    // FIXME file by file, and then delete them
    // or cp everything to /tmp
    system("cp -r .libs ../modules");

    /* TODO:
     * - do sout without muxing if possible */
    setenv ("VLC_PLUGIN_PATH", "../modules", 1);
    setenv ("VLC_DATA_PATH", "../share", 1);
    setenv ("VLC_LIB_PATH", "../modules", 1);
    libvlc = libvlc_new(7, (char const *[])
    {
        "--avcodec-hw=none", "--stop-time=5", "--play-and-exit",
        //"--sout=#transcode{vcodec=h264,vb=1000,acodec='mp3',--vfilter=deinterlace}:std{access=file,dst=video.ts}",
        "http://streams.videolan.org/streams/ts/bbc_news_24-239.35.2.0_dvbsub.ts",
        "--deinterlace-mode", mode
    });
    assert(libvlc);

    libvlc_set_exit_handler(libvlc, vlc_kill, NULL);

    shm_id = shmget(ftok("deint_cycles", 0x2A), 64, 0600 | IPC_CREAT);
    shm_addr = shmat(shm_id, NULL, 0);
    return VLC_SUCCESS;
}

static int
init_deinterlacer_linear(void)
{
    return init_deinterlacer("linear");
}

static void
destroy_deinterlacer(void)
{
    libvlc_release(libvlc);
    shmdt(shm_addr);
    shmctl(shm_id, IPC_RMID, NULL);
}

static int
check_feature_deinterlacer(int flag)
{
    VLC_UNUSED(flag);
    return 1;
}

static uint64_t
bench_deinterlacer(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    libvlc_playlist_play(libvlc);

    int signum;
    fprintf(stderr, "sigwait...\n");
    sigwait(&set, &signum);
    fprintf(stderr, "sigwait DONE %d\n", signum);

    return *(uint64_t const *)shm_addr;
}

void
subscribe_linear_deinterlacer(int id)
{
    bench_asm_subscribe(id, "linear deinterlacer",
                        init_deinterlacer_linear,
                        destroy_deinterlacer,
                        check_feature_deinterlacer,
                        bench_deinterlacer);
}
