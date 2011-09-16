/*
 * This file is part of the Shiboken Python Bindings Generator project.
 *
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: PySide team <contact@pyside.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef SBKPYTHON_H
#define SBKPYTHON_H

#include "Python.h"

#if PY_MAJOR_VERSION >= 3
    #define IS_PY3K

    #define PyInt_Type PyLong_Type
    #define PyInt_Check PyLong_Check
    #define PyInt_AS_LONG PyLong_AS_LONG
    #define PyInt_FromLong PyLong_FromLong
    #define PyInt_AsLong PyLong_AsLong
    #define SbkNumber_Check PyNumber_Check
    #define Py_TPFLAGS_CHECKTYPES  0

    #define SBK_STR_NAME "bytes"
#else
    // Note: if there wasn't for the old-style classes, only a PyNumber_Check would suffice.
    #define SbkNumber_Check(X) \
            (PyNumber_Check(X) && (!PyInstance_Check(X) || PyObject_HasAttrString(X, "__trunc__")))
    #define SBK_STR_NAME "str"
#endif

#endif