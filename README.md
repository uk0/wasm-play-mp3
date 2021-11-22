# wasm-play-mp3
自动播放音乐，无需点击。  Automatically play music without clicking.   JavaScript + Html


### wasm 相关代码来自 

https://ntzyz.io/post/decode-mpeg-layer-3-using-web-assembly-in-browser


### 感谢

https://github.com/ntzyz 

### build

#### 安装emsdk

```bash

# Get the emsdk repo
git clone https://github.com/emscripten-core/emsdk.git

# Enter that directory
cd emsdk
# Fetch the latest version of the emsdk (not needed the first time you clone)
git pull

# Download and install the latest SDK tools.
./emsdk install latest

# Make the "latest" SDK "active" for the current user. (writes .emscripten file)
./emsdk activate latest

# Activate PATH and other environment variables in the current terminal
source ./emsdk_env.sh

# use gcc8 g++8

./emsdk install sdk-upstream-main-64bit
./emsdk install binaryen-main-64bit



./emsdk  activate sdk-upstream-main-64bit
./emsdk  activate binaryen-main-64bit


```


#### 安装libmad

* 1. 下载 libmad https://sourceforge.net/projects/mad/files/libmad/0.15.1b/libmad-0.15.1b.tar.gz/download
* 2. source ./emsdk_env.sh

```bash
cd /path/to/libmad.tar.gz/
tar xzf libmad*.tar.gz
cd libmad*
mkdir ../build
emconfigure ./configure --prefix=$(pwd)/../build
emmake make
emmake make install
```



#### build-wasm

```bash
# main.c 在这个build 文件夹下
mkdir server
emcc -I../build/include -L../build/lib main.c -lmad -s WASM=1 -o server/test.html -s TOTAL_MEMORY=268435456  -s EXPORTED_FUNCTIONS="['_main','_malloc', '_decode_mp3_to_pcm']" -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall","cwrap","writeArrayToMemory"]' -O3

```

#### 查看文件目录

```bash
root@DESKTOP-PFF8EKP:/mnt/f/emsdk/build# tree .
.
├── include
│   └── mad.h
├── lib
│   ├── libmad.a
│   ├── libmad.la
│   ├── libmad.so -> libmad.so.0.2.1
│   ├── libmad.so.0 -> libmad.so.0.2.1
│   └── libmad.so.0.2.1
├── main.c
└── server
    ├── test.html
    ├── test.js
    └── test.wasm
```




