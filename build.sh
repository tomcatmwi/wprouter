# BEFORE BUILD, DOWNLOAD AND UNZIP THESE!
#
# websocketpp:
# https://github.com/zaphoyd/websocketpp.git
#
# asio:
# https://think-async.com/Asio/Download.html
#
# mipsel c++ build (if you're building for mipsel):
# https://musl.cc/mipsel-linux-muslsf-cross.tgz


# Most embedded Linux (OpenWrt) devices would need this compiler, but adjust accordingly
COMPILER_PATH="./mipsel-linux-muslsf-cross/bin/mipsel-linux-muslsf-g++"
APP_NAME="wprouter"

clear
$COMPILER_PATH -static -I./websocketpp -I./asio/include -std=c++17 -o $APP_NAME ./main.cpp
strip $APP_NAME
