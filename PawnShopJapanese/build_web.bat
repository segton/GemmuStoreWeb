@echo off
setlocal

echo === Setting up Emscripten ===
call "E:\dev\emsdk\emsdk\emsdk_env.bat"

set PROJECT_DIR=C:\Users\shuse\source\repos\PawnShopJapanese - Copy\PawnShopJapanese
set BUILD_DIR=%PROJECT_DIR%\build_web

cd /d "%PROJECT_DIR%"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo === Cleaning old web output ===
rmdir /s /q "%BUILD_DIR%" 2>nul
mkdir "%BUILD_DIR%"

echo === Compiling RayDial C files ===

call emcc -c "..\External\raydial\src\raydial.c" ^
  -I"E:\dev\raylib_web\src" ^
  -I"E:\dev\raylib_web\src\external" ^
  -I"C:\Users\shuse\source\repos\PawnShopJapanese - Copy\External\raydial\include" ^
  -O2 ^
  -DNDEBUG ^
  -DPLATFORM_WEB ^
  -o "%BUILD_DIR%\raydial.o"

if errorlevel 1 goto :error

echo === Compiled raydial.c ===

call emcc -c "..\External\raydial\src\raydial_i18n.c" ^
  -I"E:\dev\raylib_web\src" ^
  -I"E:\dev\raylib_web\src\external" ^
  -I"C:\Users\shuse\source\repos\PawnShopJapanese - Copy\External\raydial\include" ^
  -O2 ^
  -DNDEBUG ^
  -DPLATFORM_WEB ^
  -D_strdup=strdup ^
  -o "%BUILD_DIR%\raydial_i18n.o"

if errorlevel 1 goto :error

echo === Compiled raydial_i18n.c ===
echo === Building Gemmu Shoppe Web ===

call em++ ^
  main.cpp ^
  Game.cpp ^
  PhysicsWorld.cpp ^
  "%BUILD_DIR%\raydial.o" ^
  "%BUILD_DIR%\raydial_i18n.o" ^
  -I"." ^
  -I"E:\dev\raylib_web\src" ^
  -I"E:\dev\raylib_web\src\external" ^
  -I"E:\dev\libraries\JoltPhysics" ^
  -I"C:\Users\shuse\source\repos\PawnShopJapanese - Copy\External\raydial\include" ^
  "E:\dev\raylib_web\build_web_es3\raylib\libraylib.a" ^
  "E:\dev\libraries\JoltPhysics\BuildWeb\libJolt.a" ^
  -std=c++17 ^
  -O2 ^
  -DNDEBUG ^
  -DPLATFORM_WEB ^
  -DJPH_PROFILE_ENABLED ^
  -DJPH_DEBUG_RENDERER ^
  -DJPH_OBJECT_STREAM ^
  -sUSE_GLFW=3 ^
  -sWASM=1 ^
  -sALLOW_MEMORY_GROWTH=1 ^
  -sINITIAL_MEMORY=1024MB ^
  -sSTACK_SIZE=8388608 ^
  -sMIN_WEBGL_VERSION=2 ^
  -sMAX_WEBGL_VERSION=2 ^
  -sASSERTIONS=0 ^
  -sSAFE_HEAP=0 ^
  -sGL_ASSERTIONS=0 ^
  --shell-file shell_minimal.html ^
  --preload-file Audio@Audio ^
  --preload-file Models@Models ^
  --preload-file Shaders@Shaders ^
  --preload-file Data@Data ^
  --preload-file Textures@Textures ^
  --preload-file Fonts@Fonts ^
  --preload-file resources@resources ^
  -o "%BUILD_DIR%\index.html"

if errorlevel 1 goto :error

echo === Build succeeded ===
echo Output is in build_web
pause
exit /b 0

:error
echo === Build failed ===
pause
exit /b 1