call "C:\v2019\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH_OLD=%PATH%
call C:\emsdk\emsdk_env.bat
set PATH=%PATH%:%PATH_OLD%
cmake -DCMAKE_BUILD_TYPE=Release  -G "CodeBlocks - NMake Makefiles" . -B web "-DCMAKE_EXE_LINKER_FLAGS=-s BINARYEN_ASYNC_COMPILATION=1 -s ALLOW_MEMORY_GROWTH=1 -s WASM=1 -s USE_GLFW=3 -s USE_WEBGL2=1 -s FULL_ES3=1 -s ASSERTIONS=0 --preload-file resources\@resources  --use-preload-plugins" -DPLATFORM=Web -DCMAKE_TOOLCHAIN_FILE=c:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake  -DNCNN_VULKAN=OFF -DNCNN_SYSTEM_GLSLANG=OFF -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BENCHMARK=OFF -DNCNN_BUILD_TOOLS=OFF -DCMAKE_CROSSCOMPILING_EMULATOR="C:/emsdk/node/12.18.1_64bit/bin/node.exe" && cmake.exe --build web