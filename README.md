# Arithmetic Codec
Arithmetic encoder/decoder, ported in C from Amir Said's FastAC.

It's one header file library, put those line in a cpp/c file.

````C

#define __ARITHMETIC_CODEC__IMPLEMENTATION__
#include "../arithmetic_codec.h"

````



### Unit tests build status (Linux/MacOs/Windows)
[![Build Status](https://github.com/geolm/arithmetic_codec/actions/workflows/build.yml/badge.svg)](https://github.com/geolm/arithmetic_codec/actions)


# License

From the code:
```C
// The only purpose of this program is to demonstrate the basic principles   -
// of arithmetic coding. It is provided as is, without any express or        -
// implied warranty, without even the warranty of fitness for any particular -
// purpose, or that the implementations are correct.                         -
//                                                                           -
// Permission to copy and redistribute this code is hereby granted, provided -
// that this warning and copyright notices are not removed or altered.       -
//                                                                           -
// Copyright (c) 2004 by Amir Said (said@ieee.org) &                         -
//                       William A. Pearlman (pearlw@ecse.rpi.edu)   
