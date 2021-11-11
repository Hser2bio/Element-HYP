#!/bin/bash
#------------Prepare the environment
echo @@@
echo @@@"Installing Dependecies"
echo @@@
sudo apt-get install software-properties-common -y
sudo add-apt-repository ppa:pivx/pivx -y
sudo apt-get update -y
sudo apt-get install -y -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" make software-properties-common \
build-essential libtool autoconf  libboost-dev libboost-chrono-dev libboost-filesystem-dev libboost-program-options-dev \
libboost-system-dev libboost-test-dev libboost-thread-dev sudo automake git wget curl libdb4.8-dev bsdmainutils libdb4.8++-dev \
libminiupnpc-dev libgmp3-dev ufw pkg-config libevent-dev  libdb5.3++ unzip libzmq5 libssl-dev
sudo apt install -y curl gcc-mingw-w64-i686 gcc-mingw-w64-base  imagemagick librsvg2-bin g++-mingw-w64-x86-64 nsis bc  binutils-arm-linux-gnueabihf g++-7-multilib gcc-7-multilib binutils-gold git pkg-config autoconf libtool automake bsdmainutils ca-certificates python g++ mingw-w64 g++-mingw-w64 nsis zip rename librsvg2-bin libtiff-tools cmake imagemagick libcap-dev libz-dev libbz2-dev python-dev python-setuptools fonts-tuffy
sudo apt-get install g++-mingw-w64-i686 -y
mkdir -p ~/release
cd /root/Element-HYP/depends/

#------------Compile w32 depedencies
sudo make HOST=i686-w64-mingw32 -j8
cd ..

#------------Compile w32 core
sudo ./autogen.sh && sudo ./configure --prefix=`pwd`/depends/i686-w64-mingw32 --disable-tests && sudo make -j8

#------------Make install
sudo make install DESTDIR=/root/release/Element-HYP-w32/ -j8
echo "Finished, you could find the release in /root/release/Element-HYP-w32"
