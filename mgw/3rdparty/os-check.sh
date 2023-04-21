#!/bin/bash

SUDO=sudo

function require_sudoer()
{
    sudo echo "" >/dev/null 2>&1
    
    ret=$?; if [[ 0 -ne $ret ]]; then 
        echo "\"$1\" require sudoer failed. ret=$ret";
        exit $ret; 
    fi
}

#check gcc,g++,make,cmake,patch,unzip,valgrind,pkg-config,tclsh
OS_TYPE=
function Env_Check()
{
    OS_INSTALL_CMD=$1
    if [[ -z $OS_INSTALL_CMD ]]; then
        echo "os install cmd is null!"
        exit 1
    fi

    NOBEST=
    FORCE_YES=
    if [[ $OS_INSTALL_CMD = "yum" ]]; then
        echo -e "install by $OS_INSTALL_CMD";
        $SUDO yum --help | grep nobest >/dev/null 2>&1; ret=$?; if [[ 0 -eq $ret ]]; then NOBEST=--nobest; fi
    elif [[ $OS_INSTALL_CMD = "apt-get" ]]; then
        $SUDO apt-get update && apt-get install sudo;
        echo -e "install by $OS_INSTALL_CMD"; FORCE_YES=--force-yes;
    fi

    $OS_INSTALL_CMD --version >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo -e "Do not support this install cmd: $OS_INSTALL_CMD"
        exit 1
    fi
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc."
        require_sudoer "sudo $OS_INSTALL_CMD install -y gcc"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc is installed."
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc-c++."
        require_sudoer "sudo $OS_INSTALL_CMD install -y gcc-c++"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y gcc-c++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc-c++ is installed."
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing make."
        require_sudoer "sudo $OS_INSTALL_CMD install -y make"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The make is installed."
    fi

    cmake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing cmake."
        require_sudoer "sudo  $OS_INSTALL_CMD install -y cmake"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y cmake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The cmake is installed."
    fi
    
    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing patch."
        require_sudoer "sudo $OS_INSTALL_CMD install -y patch"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The patch is installed."
    fi
    
    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing unzip."
        require_sudoer "sudo $OS_INSTALL_CMD install -y unzip"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The unzip is installed."
    fi

    if [[ $MGW_VALGRIND == YES ]]; then
        valgrind --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing valgrind."
            require_sudoer "sudo $OS_INSTALL_CMD install -y valgrind"
            $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y valgrind; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The valgrind is installed."
        fi
    fi

    if [[ $MGW_VALGRIND == YES ]]; then
        if [[ ! -f /usr/include/valgrind/valgrind.h ]]; then
            echo "Installing valgrind-devel."
            require_sudoer "sudo $OS_INSTALL_CMD install -y valgrind-devel"
            $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y valgrind-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The valgrind-devel is installed."
        fi
    fi

    tclsh <<< "exit" >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing tcl."
        require_sudoer "sudo $OS_INSTALL_CMD install -y tcl"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y tcl; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The tcl is installed."
    fi

    tar --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing tar."
        require_sudoer "sudo $OS_INSTALL_CMD install -y tar"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y tar; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The tar is installed."
    fi

    autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing autoconf."
        require_sudoer "sudo $OS_INSTALL_CMD install -y autoconf"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The autoconf is installed."
    fi

    automake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing automake."
        require_sudoer "sudo $OS_INSTALL_CMD install -y automake"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y automake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The automake is installed."
    fi

    libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing libtool."
        require_sudoer "sudo $OS_INSTALL_CMD install -y libtool"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The libtool is installed."
    fi

    pkg-config --version --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing pkg-config."
        require_sudoer "sudo $OS_INSTALL_CMD install -y pkg-config"
        $SUDO $OS_INSTALL_CMD install ${NOBEST} ${FORCE_YES} -y pkg-config; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The pkg-config is installed."
    fi

    echo -e "Tools for $OS_TYPE are installed."
    return 0
}

function OS_Check()
{
    uname -v|grep Ubuntu >/dev/null 2>&1
    ret=$?;
    if [[ 0 = $ret ]]; then
        OS_TYPE=ubuntu
        return 0;
    elif [[ -f /etc/debian_version ]]; then
        # for debian, we think it's ubuntu also.
        # for example, the wheezy/sid which is debian armv7 linux, can not identified by uname -v.
        OS_TYPE=ubuntu
        return 0;
    fi

    # for rocky, we think it's centos also.
    if [[ -f /etc/redhat-release ]]; then
        OS_TYPE=centos
        return 0;
    fi

    return 1
}

OS_Check; ret=$?; if [[ 0 -ne $ret && -z $OS_TYPE ]]; then
    echo -e "Your OS `uname -s` is not supported."
    exit 1
else
    echo -e "os type: $OS_TYPE"

    if [[ $OS_TYPE = "ubuntu" ]]; then
        Env_Check apt-get; ret=$?; if [[ 0 -ne $ret ]]; then echo "check ubuntu failed!"; exit 1; fi
    elif [[ $OS_TYPE = "centos" ]]; then
        Env_Check yum; ret=$?; if [[ 0 -ne $ret ]]; then echo "check centos failed!"; exit 1; fi
    else
        echo -e "os type: $OS_TYPE is not supported."
        exit 1
    fi

    ./thirdparty-check.sh $OS_TYPE; ret=$?; if [[ 0 -ne $ret ]]; then
        echo -e "Check thirdparty for $OS_TYPE failed!"; exit $ret;
    fi
fi