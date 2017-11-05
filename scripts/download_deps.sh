#!/bin/bash

#
# Download dependencies
#

set -e

BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

source $BASE_DIR/scripts/common.sh

# Initialize some directories
mkdir -p $DOWNLOAD_DIR

download()
{
    archive=$DOWNLOAD_DIR/$1
    url=$2

    if [[ ! -e $archive ]]; then
        echo "Downloading $url..."
        curl -L -o $archive $url
    fi
}

clone()
{
    archive=$DOWNLOAD_DIR/$1
    url=$2
    tag=$3

    if [[ ! -e $archive ]]; then
        echo "Cloning $url..."

        tmpdir=$DOWNLOAD_DIR/tmp
        rm -fr $tmpdir
        mkdir -p $tmpdir
        pushd $tmpdir
        git clone $url

        cd $(basename $url .git)
        git checkout $tag
        git archive --format=tar --prefix=$(basename $archive .tar.xz)/ $tag | xz -c > $archive
        popd
        rm -fr $tmpdir
    fi
}

clone zlib-$ZLIB_VERSION.tar.xz https://github.com/Dead2/zlib-ng.git $ZLIB_VERSION
#download zlib-$ZLIB_VERSION.tar.xz http://zlib.net/zlib-$ZLIB_VERSION.tar.xz
download confuse-$CONFUSE_VERSION.tar.xz https://github.com/martinh/libconfuse/releases/download/v$CONFUSE_VERSION/confuse-$CONFUSE_VERSION.tar.xz
download libarchive-$LIBARCHIVE_VERSION.tar.gz http://libarchive.org/downloads/libarchive-$LIBARCHIVE_VERSION.tar.gz
download libsodium-$LIBSODIUM_VERSION.tar.gz https://github.com/jedisct1/libsodium/releases/download/$LIBSODIUM_VERSION/libsodium-$LIBSODIUM_VERSION.tar.gz

