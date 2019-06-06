#!/bin/sh

set -e
set -x

info()
{
    local green="\033[1;32m"
    local normal="\033[0m"
    echo "[${green}build${normal}] $1"
}

usage()
{
cat << EOF
usage: $0 [options]

Build vlc in the current directory

OPTIONS:
   -h            Show some help
   -r            Release mode (default is debug)
   -a <arch>     Use the specified arch (default: x86_64, possible i686, aarch64)
   -p            Use a Prebuilt contrib package (speeds up compilation)
   -c            Create a Prebuilt contrib package (rarely used)
   -l            Enable translations (can be slow)
   -i <n|r|u|m>  Create an Installer (n: nightly, r: release, u: unsigned release archive, m: msi only)
   -s            Interactive shell (get correct environment variables for build)
   -b <url>      Enable breakpad support and send crash reports to this URL
   -d            Create PDB files during the build
EOF
}

ARCH="x86_64"
while getopts "hra:pcli:sb:d" OPTION
do
     case $OPTION in
         h)
             usage
             exit 1
         ;;
         r)
             RELEASE="yes"
             INSTALLER="r"
         ;;
         a)
             ARCH=$OPTARG
         ;;
         p)
             PREBUILT="yes"
         ;;
         c)
             PACKAGE="yes"
         ;;
         l)
             I18N="yes"
         ;;
         i)
             INSTALLER=$OPTARG
         ;;
         s)
             INTERACTIVE="yes"
         ;;
         b)
             BREAKPAD=$OPTARG
         ;;
         d)
             WITH_PDB="yes"
         ;;
     esac
done
shift $(($OPTIND - 1))

if [ "x$1" != "x" ]; then
    usage
    exit 1
fi

case $ARCH in
    x86_64)
        SHORTARCH="win64"
        ;;
    i686)
        SHORTARCH="win32"
        ;;
    aarch64)
        SHORTARCH="winarm64"
        ;;
    *)
        usage
        exit 1
esac

#####

SCRIPT_PATH="$( cd "$(dirname "$0")" ; pwd -P )"

: ${JOBS:=$(getconf _NPROCESSORS_ONLN 2>&1)}
TRIPLET=$ARCH-w64-mingw32

info "Building extra tools"
mkdir -p tools
cd tools
export VLC_TOOLS="$PWD/build"
export PATH="$VLC_TOOLS/bin":"$PATH"
# bootstrap only if needed in interactive mode
FORCED_TOOLS=
if [ ! -d libtool ]; then
	# TODO only force if building with CLang
	FORCED_TOOLS="$FORCED_TOOLS libtool"
fi
if [ "$INTERACTIVE" != "yes" ] || [ ! -f ./Makefile ]; then
    NEEDED=${FORCED_TOOLS} ${SCRIPT_PATH}/../../tools/bootstrap
fi
make -j$JOBS
cd ..

export USE_FFMPEG=1
export PKG_CONFIG_LIBDIR="$PWD/contrib/$TRIPLET/lib/pkgconfig"
export PATH="$PWD/contrib/$TRIPLET/bin":"$PATH"

if [ "$INTERACTIVE" = "yes" ]; then
if [ "x$SHELL" != "x" ]; then
    exec $SHELL
else
    exec /bin/sh
fi
fi

info "Building contribs"

mkdir -p contrib-$SHORTARCH && cd contrib-$SHORTARCH
if [ ! -z "$WITH_PDB" ]; then
    CONTRIBFLAGS="$CONTRIBFLAGS --enable-pdb"
fi
if [ ! -z "$BREAKPAD" ]; then
     CONTRIBFLAGS="$CONTRIBFLAGS --enable-breakpad"
fi
PKG_CONFIG_PATH="" ${SCRIPT_PATH}/../../../contrib/bootstrap --host=$TRIPLET $CONTRIBFLAGS
# do not use the triple-pkg-config, as it may appear to be available when it's done (Debian)
export PKG_CONFIG=`which pkg-config`

# Rebuild the contribs or use the prebuilt ones
if [ "$PREBUILT" != "yes" ]; then
make list
make -j$JOBS fetch
make -j$JOBS -k || make -j1
if [ "$PACKAGE" = "yes" ]; then
make package
fi
else
make prebuilt
make .luac
fi
cd ..

info "Bootstrapping"

${SCRIPT_PATH}/../../../bootstrap

info "Configuring VLC"
mkdir $SHORTARCH || true
cd $SHORTARCH

CONFIGFLAGS=""
if [ "$RELEASE" != "yes" ]; then
     CONFIGFLAGS="$CONFIGFLAGS --enable-debug"
else
     CONFIGFLAGS="$CONFIGFLAGS --disable-debug"
fi
if [ "$I18N" != "yes" ]; then
     CONFIGFLAGS="$CONFIGFLAGS --disable-nls"
fi
if [ ! -z "$BREAKPAD" ]; then
     CONFIGFLAGS="$CONFIGFLAGS --with-breakpad=$BREAKPAD"
fi
if [ ! -z "$WITH_PDB" ]; then
    CONFIGFLAGS="$CONFIGFLAGS --enable-pdb"
fi

${SCRIPT_PATH}/configure.sh --host=$TRIPLET --with-contrib=../contrib-$SHORTARCH/$TRIPLET $CONFIGFLAGS

info "Compiling"
make -j$JOBS

if [ "$INSTALLER" = "n" ]; then
make package-win32-debug package-win32 package-msi
elif [ "$INSTALLER" = "r" ]; then
make package-win32
elif [ "$INSTALLER" = "u" ]; then
make package-win32-release
sha512sum vlc-*-release.7z
elif [ "$INSTALLER" = "m" ]; then
make package-msi
fi
