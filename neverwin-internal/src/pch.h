#pragma once

// Версия сборки DLL. Передаётся при сборке (-DNW_VERSION=3), 0 = собрано вручную.
#ifndef NW_VERSION
#define NW_VERSION 0
#endif

// Общие флаги сборки.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
