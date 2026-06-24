#pragma once
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <map>
using namespace std;

// NT kernel types for ObCreateSymbolicLink (from FSD)
typedef struct _STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} STRING;

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

extern "C" int ObCreateSymbolicLink(STRING*, STRING*);
extern "C" int ObDeleteSymbolicLink(STRING*);
