#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <climits>
#include <cstdint>

typedef void* HWND;
typedef void* HANDLE;
typedef intptr_t INT_PTR;
typedef unsigned int UINT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef uint32_t DWORD;

#define CALLBACK

#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#endif
