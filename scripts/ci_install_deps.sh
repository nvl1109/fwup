#!/bin/bash

#
# Install dependencies on CircleCi
#
# Inputs:
#    CIRCLE_OS_NAME - "linux" or "osx"
#    BUILD_STATIC   - "true" or "false"
#
# Static builds use scripts to download libarchive, libconfuse, and libsodium,
# so those are only installed on shared library builds.
#

set -e
set -v

source scripts/third_party_versions.sh

MAKE_FLAGS=-j4
if [[ "$CIRCLE_OS_NAME" = "linux" ]]; then
    DEPS_INSTALL_DIR=/usr
else
    DEPS_INSTALL_DIR=/usr/local
fi

install_confuse() {
    curl -LO https://github.com/martinh/libconfuse/releases/download/v$CONFUSE_VERSION/confuse-$CONFUSE_VERSION.tar.xz
    tar xf confuse-$CONFUSE_VERSION.tar.xz
    pushd confuse-$CONFUSE_VERSION
    ./configure --prefix=$DEPS_INSTALL_DIR --disable-examples
    make $MAKE_FLAGS
    make install
    popd
}

install_sodium() {
    curl -LO https://github.com/jedisct1/libsodium/releases/download/$LIBSODIUM_VERSION/libsodium-$LIBSODIUM_VERSION.tar.gz
    tar xf libsodium-$LIBSODIUM_VERSION.tar.gz
    pushd libsodium-$LIBSODIUM_VERSION
    ./configure --prefix=$DEPS_INSTALL_DIR
    make $MAKE_FLAGS
    make install
    popd
}

if [[ "$CIRCLE_OS_NAME" = "linux" ]]; then
    apt-get update -qq
    apt-get install -qq autopoint dosfstools mtools unzip zip help2man autoconf build-essential libtool curl pkg-config mtools unzip zip help2man ca-certificates
    case $MODE in
        windows)
            dpkg --add-architecture i386
            apt-get update
            apt-get install -qq gcc-mingw-w64-x86-64 wine
            ;;
        singlethread|dynamic)
            apt-get install -qq libarchive-dev python-pip python-dev
            install_confuse
            install_sodium
            pip install --user cpp-coveralls
            ;;
        static)
            # Need fpm when building static so that we can make the .deb and .rpm packages
            apt-get install -qq rpm rubygems ruby-dev 
            gem install fpm --no-ri --no-rdoc
            ;;
        raspberrypi)
            apt-get install -qq libarchive-dev qemu binfmt-support qemu-user-static rubygems ruby-dev rpm
            gem install fpm --no-ri --no-rdoc
            pushd ~
            git clone https://github.com/raspberrypi/tools.git --depth 1
            popd
            ;;

    esac
else
    # OSX
    brew update

    # Fix "/usr/local/Library/ENV/4.3/sed: Not such file" errors
    brew uninstall libtool
    brew install libtool

    brew install mtools
    brew install gettext
    if [[ "$MODE" = "dynamic" ]]; then
        brew install libarchive libsodium confuse
    fi
    # Fix brew breakage in autotools
    mkdir -p /usr/local/Library/ENV
    ln -s /usr/local/Library/Homebrew/shims/super /usr/local/Library/ENV/4.3
    ls /usr/local/Library/ENV/4.3
fi

