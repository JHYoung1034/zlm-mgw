#!/bin/bash

MGW_3RDPARTY=`pwd`
INSTALL_PREFIX=${MGW_3RDPARTY}/install
OS_TYPE=$1
SUDO=sudo

if [[ -z $OS_TYPE ]]; then
    echo "Please input a os type[ubuntu/centos]!"
    exit 1
elif [[ $OS_TYPE != "ubuntu" && $OS_TYPE != "centos" ]]; then
    echo "Please input a os type[ubuntu/centos]!"
    exit 1
fi

function require_sudoer()
{
    sudo echo "" >/dev/null 2>&1
    
    ret=$?; if [[ 0 -ne $ret ]]; then 
        echo "\"$1\" require sudoer failed. ret=$ret";
        exit $ret; 
    fi
}

function Ubuntu_Check_3rdparty()
{
#check libz
    dpkg -s zlib1g-dev >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing libz."
        require_sudoer "sudo apt-get install -y --force-yes zlib1g-dev"
        $SUDO apt-get install -y --force-yes zlib1g-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The zlib is installed."
    fi

#check openssl
    dpkg -s libssl-dev >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing openssl."
        require_sudoer "sudo apt-get install -y --force-yes libssl-dev"
        $SUDO apt-get install -y --force-yes libssl-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The openssl is installed."
    fi

#check yasm for x265
    yasm --version >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing yasm."
        require_sudoer "sudo apt-get install -y --force-yes yasm"
        $SUDO apt-get install -y --force-yes yasm; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The yasm is installed."
    fi
}

function CentOS_Check_3rdparty()
{
#check libz
    yum list installed | grep zlib-devel >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing libz."
        require_sudoer "sudo yum install -y zlib-devel"
        $SUDO yum install -y zlib-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The zlib is installed."
    fi

#check openssl
    yum list installed | grep openssl-devel >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing openssl."
        require_sudoer "sudo yum install -y openssl-devel"
        $SUDO yum install -y openssl-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The openssl is installed."
    fi
}

if [[ $OS_TYPE = "ubuntu" ]]; then
    Ubuntu_Check_3rdparty;
elif [[ $OS_TYPE = "centos" ]]; then
    CentOS_Check_3rdparty;
fi
ret=$?; if [[ 0 -ne $ret ]]; then echo -e "Check thirdparty for $OS_TYPE failed!"; exit $ret; fi

################### compile nasm for libx264 ###################################
if [[ -f ${MGW_3RDPARTY}/install/bin/nasm ]]; then
    echo "nasm-2.15.05-fit is ok.";
else
    echo "Build nasm-2.15.05"
    (
        if [[ ! -f ${MGW_3RDPARTY}/nasm-2.15.05-fit.tar.gz ]]; then
            echo "Do not exists nasm packet: 'nasm-2.15.05-fit.tar.gz'";
            exit -1;
        fi

        # Start build nasm.
        cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/nasm-2.15.05 &&
        tar -zxvf nasm-2.15.05-fit.tar.gz && cd nasm-2.15.05 &&
        ./configure --prefix=${INSTALL_PREFIX} &&
        make ${MGW_JOBS} && make strip && make install &&
        cd .. && rm -rf nasm-2.15.05
    )
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build nasm-2.15.05-fit failed, ret=$ret"; exit $ret; fi
fi

if [ ! -f ${MGW_3RDPARTY}/install/bin/nasm ]; then echo "Build nasm-2.15.05-fit failed."; exit -1; fi

export PATH=$PATH:${INSTALL_PREFIX}/bin

################### compile libprotobuf3.14.0 ##################################
if [[ -f ${MGW_3RDPARTY}/install/lib/libprotobuf.so ]]; then
    echo "protobuf-all-3.14.0-fit is ok.";
else
    echo "Build protobuf-all-3.14.0-fit"
    (
        if [[ ! -f ${MGW_3RDPARTY}/protobuf-all-3.14.0-fit.tar.gz ]]; then
            echo "Do not exists protobuf packet: 'protobuf-all-3.14.0-fit.tar.gz'";
            exit -1;
        fi

        PROTOBUF_OPTIONS="--enable-static=no"
        # Start build protobuf.
        cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/protobuf-3.14.0 &&
        tar -zxvf protobuf-all-3.14.0-fit.tar.gz && cd protobuf-3.14.0 &&
        ./autogen.sh && ./configure --prefix=${INSTALL_PREFIX} $PROTOBUF_OPTIONS &&
        make ${MGW_JOBS} && make install &&
        cd .. && rm -rf protobuf-3.14.0
    )
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build protobuf-3.14.0-fit failed, ret=$ret"; exit $ret; fi
fi

if [ ! -f ${MGW_3RDPARTY}/install/lib/libprotobuf.so ]; then echo "Build protobuf-3.14.0-fit failed."; exit -1; fi

################### compile libsrt-1.4.4 #######################################
if [[ -f ${MGW_3RDPARTY}/install/lib/libsrt.so ]]; then
    echo "srt-1.4.4-fit is ok.";
else
    echo "Build srt-1.4.4-fit"
    (
        if [[ ! -f ${MGW_3RDPARTY}/srt-1.4.4-fit.tar.gz ]]; then
            echo "Do not exists srt packet: 'srt-1.4.4-fit.tar.gz'";
            exit -1;
        fi

        #Check pkgconfig of openssl, exit if exist
        pkg-config --exists libssl >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "libssl no found, build srt-1.4.4-fit failed.";
            exit -1;
        fi

        # Always disable c++11 for libsrt, because only the srt-app requres it.
        LIBSRT_OPTIONS="--disable-app  --enable-static=0 --enable-shared=1 --enable-c++11=0"
        # if [[ $MGW_SHARED_LIBS == YES ]]; then
        #     LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=1"
        # else
        #     LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=0"
        # fi
        # Start build libsrt.
        cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/srt &&
        tar -zxvf srt-1.4.4-fit.tar.gz && cd srt &&
        ./configure --prefix=${INSTALL_PREFIX} $LIBSRT_OPTIONS &&
        make ${MGW_JOBS} && make install &&
        cd .. && rm -rf srt &&
        #If exists lib64 of libsrt, link it to lib
        if [[ -d ./install/lib64 ]]; then
            cd ./install
            if [[ ! -d lib ]]; then mkdir lib; fi
            cp -rvf lib64/* lib/
        fi
    )
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build srt-1.4.4-fit failed, ret=$ret"; exit $ret; fi
fi

if [ ! -f ${MGW_3RDPARTY}/install/lib/libsrt.so ]; then echo "Build srt-1.4.4-fit failed."; exit -1; fi

#check ffmpeg4.x  --> x264, x265,rtmp,fdk-aac
function ffmpeg_install_check()
{
    #libx264
    if [[ -f ${MGW_3RDPARTY}/install/lib/libx264.so.157 ]]; then
        echo "libx264-20191227-stable-fit is ok.";
    else
        echo "Build libx264-20191227-stable-fit"
        (
            if [[ ! -f ${MGW_3RDPARTY}/libx264-20191227-stable-fit.tar.gz ]]; then
                echo "Do not exists libx264 packet: 'libx264-20191227-stable-fit.tar.gz'";
                exit -1;
            fi

            # Start build libx264.
            LIBX264_OPTIONS="--enable-shared --disable-static --disable-cli"
            cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/libx264 &&
            tar -zxvf libx264-20191227-stable-fit.tar.gz && cd libx264 &&
            ./configure --prefix=${INSTALL_PREFIX} $LIBX264_OPTIONS &&
            make ${MGW_JOBS} && make install && cd .. && rm -rf libx264
        )
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build libx264-20191227-stable-fit failed, ret=$ret"; exit $ret; fi
    fi

    if [ ! -f ${MGW_3RDPARTY}/install/lib/libx264.so.157 ]; then echo "Build libx264-20191227-stable-fit failed."; exit -1; fi

    #x265
    if [[ -f ${MGW_3RDPARTY}/install/lib/libx265.so ]]; then
        echo "x265-3.2-fit is ok.";
    else
        echo "Build x265-3.2-fit"
        (
            if [[ ! -f ${MGW_3RDPARTY}/x265-3.2-fit.tar.gz ]]; then
                echo "Do not exists x265 packet: 'x265-3.2-fit.tar.gz'";
                exit -1;
            fi

            # Start build x265.
            X265_OPTIONS="-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} -DENABLE_STATIC=OFF"
            cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/x265_3.2 &&
            tar -xvf x265-3.2-fit.tar.gz && cd x265_3.2 &&
            mkdir built && cd built && cmake ${X265_OPTIONS} ../source &&
            make ${MGW_JOBS} && make install && cd ../.. && rm -rf x265_3.2 &&
            rm ${MGW_3RDPARTY}/install/lib/libx265.a
        )
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build x265-3.2-fit failed, ret=$ret"; exit $ret; fi
    fi

    if [ ! -f ${MGW_3RDPARTY}/install/lib/libx265.so ]; then echo "Build x265-3.2-fit failed."; exit -1; fi

    #fdk-aac
    if [[ -f ${MGW_3RDPARTY}/install/lib/libfdk-aac.so.2.0.2 ]]; then
        echo "fdk-aac-2.0.2-fit is ok.";
    else
        echo "Build fdk-aac-2.0.2-fit"
        (
            if [[ ! -f ${MGW_3RDPARTY}/fdk-aac-2.0.2-fit.tar.gz ]]; then
                echo "Do not exists fdk-aac packet: 'fdk-aac-2.0.2-fit.tar.gz'";
                exit -1;
            fi

            # Start build fdk-aac.
            FDK_AAC_OPTIONS="--disable-static"
            cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/fdk-aac-2.0.2 &&
            tar -zxvf fdk-aac-2.0.2-fit.tar.gz && cd fdk-aac-2.0.2 && ./autogen.sh &&
            ./configure --prefix=${INSTALL_PREFIX} $FDK_AAC_OPTIONS &&
            make ${MGW_JOBS} && make install && cd .. && rm -rf fdk-aac-2.0.2
        )
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build fdk-aac-2.0.2-fit failed, ret=$ret"; exit $ret; fi
    fi

    if [ ! -f ${MGW_3RDPARTY}/install/lib/libfdk-aac.so.2.0.2 ]; then echo "Build fdk-aac-2.0.2-fit failed."; exit -1; fi

    #ffmpeg4.3
    if [[ -f ${MGW_3RDPARTY}/install/bin/ffmpeg ]]; then
        echo "ffmpeg-4.3.4-fit is ok.";
    else
        echo "Build ffmpeg-4.3.4-fit"
        (
            if [[ ! -f ${MGW_3RDPARTY}/ffmpeg-4.3.4-fit.tar.gz ]]; then
                echo "Do not exists ffmpeg packet: 'ffmpeg-4.3.4-fit.tar.gz'";
                exit -1;
            fi

            # Start build ffmpeg4.3.4.
            FFMPEG_PKGCONFIG="env PKG_CONFIG_PATH=${PKG_CONFIG_PATH}:${MGW_3RDPARTY}/install/lib/pkgconfig"
            export PKG_CONFIG_PATH=${PKG_CONFIG_PATH}:${MGW_3RDPARTY}/install/lib/pkgconfig
            export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${INSTALL_PREFIX}/lib/

            cd ${MGW_3RDPARTY} && rm -rf ${MGW_3RDPARTY}/ffmpeg-4.3.4 &&
            tar -zxvf ffmpeg-4.3.4-fit.tar.gz && cd ffmpeg-4.3.4 &&
            #Build ffmpeg4.3.4 source code
            ./configure --prefix=${INSTALL_PREFIX} --pkg-config=pkg-config \
                --extra-libs="-lssl -lcrypto -ldl -lpthread -lm" \
                --disable-everything --enable-static --disable-shared --enable-gpl --enable-nonfree \
                --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
                --disable-avdevice --disable-postproc --disable-dct --disable-dwt --disable-error-resilience --disable-lsp \
                --disable-lzo --disable-faan --disable-pixelutils --disable-hwaccels --disable-devices --disable-audiotoolbox \
                --disable-videotoolbox --disable-cuvid --disable-d3d11va --disable-dxva2 --disable-ffnvcodec --disable-nvdec \
                --disable-nvenc --disable-v4l2-m2m --disable-vaapi --disable-vdpau --disable-appkit --disable-coreimage \
                --disable-avfoundation --disable-securetransport --disable-iconv --disable-lzma --disable-sdl2 --enable-demuxer=mpegts \
                --enable-muxer=mpegts --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm --enable-encoder=aac \
                --enable-encoder=fdkaac --enable-libfdk-aac --enable-encoder=libx264 --enable-decoder=h264 --enable-libx264 \
                --enable-encoder=libx265 --enable-decoder=h265 --enable-libx265 \
                --extra-ldflags="-L${INSTALL_PREFIX}/lib/" --extra-cflags="-I${INSTALL_PREFIX}/include" &&
            make ${MGW_JOBS} && make install && cd .. && rm -rf ffmpeg-4.3.4
        )
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build ffmpeg-4.3.4-fit failed, ret=$ret"; exit $ret; fi
    fi

    if [ ! -f ${MGW_3RDPARTY}/install/bin/ffmpeg ]; then echo "Build ffmpeg-4.3.4-fit failed."; exit -1; fi
}

ffmpeg_install_check; ret=$?; if [[ 0 -ne $ret ]]; then echo "Build and install ffmpeg failed!"; exit $ret; fi
strip ${INSTALL_PREFIX}/lib/lib*.so*