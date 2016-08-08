# libatapp

基于[libatbus](https://github.com/atframework/libatbus)（树形结构进程见通信库）的服务器应用框架库

> Build & Run Unit Test in |  Linux+OSX(clang+gcc) | Windows+MinGW(vc+gcc) |
> -------------------------|--------|---------|
> Status |  暂未配置CI | 暂未配置CI |
> Compilers | linux-gcc-4.4 <br /> linux-gcc-4.6 <br /> linux-gcc-4.9 <br /> linux-gcc-6 <br /> linux-clang-3.5 <br /> osx-apple-clang-6.0 <br /> | MSVC 12(Visual Studio 2013) <br /> MSVC 14(Visual Studio 2015) <br />Mingw32-gcc <br \> Mingw64-gcc
>

Gitter
------
[![Gitter](https://badges.gitter.im/atframework/common.svg)](https://gitter.im/atframework/common?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

依赖
------

+ 支持c++0x或c++11的编译器(为了代码尽量简洁,特别是少做无意义的平台兼容，依赖部分 C11和C++11的功能，所以不支持过低版本的编译器)
> + GCC: 4.4 及以上（建议gcc 4.8.1及以上）
> + Clang: 3.4 及以上
> + VC: 12 及以上 （建议VC 14及以上）

+ [cmake](https://cmake.org/download/) 3.1.0 以上(建议 3.4 以上)
+ [msgpack](https://github.com/msgpack/msgpack-c)（用于协议打解包,仅使用头文件）
+ [libuv](http://libuv.org/)（用于网络通道）
+ [atframe_utils](https://github.com/atframework/atframe_utils)（基础公共代码）
+ [libatbus](https://github.com/atframework/libatbus)（树形结构进程见通信库）
+ [libiniloader](https://github.com/owt5008137/libiniloader)（ini读取）

Sample
------
See [sample](sample)
