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

#include "sbkstring.h"

namespace Shiboken
{

namespace String
{

bool checkType(PyTypeObject* type)
{
    return type == &PyUnicode_Type
#if PY_MAJOR_VERSION < 3
            || type == &PyString_Type
#endif
    ;
}

bool check(PyObject* obj)
{
    return obj == Py_None ||
#if PY_MAJOR_VERSION < 3
        PyString_Check(obj) ||
#endif
        PyUnicode_Check(obj);
}

bool checkChar(PyObject* pyobj)
{
    if (check(pyobj) && (len(pyobj) == 1))
        return true;

    return false;
}

bool convertible(PyObject* obj)
{
    return check(obj);
}

PyObject* fromCString(const char* value)
{
#if PY_MAJOR_VERSION >= 3
    return PyUnicode_FromString(value);
#else
    return PyBytes_FromString(value);
#endif
}

const char* toCString(PyObject* str)
{
    if (str == Py_None)
        return NULL;
#if PY_MAJOR_VERSION >= 3
    return _PyUnicode_AsString(str);
#else
    return PyBytes_AS_STRING(str);
#endif
}

bool concat(PyObject** val1, PyObject* val2)
{
    if (PyUnicode_Check(val1) && PyUnicode_Check(val2)) {
        PyObject* result = PyUnicode_Concat(*val1, val2);
        Py_DECREF(*val1);
        *val1 = result;
        return true;
    }

    if (PyBytes_Check(val1) && PyBytes_Check(val2)) {
        PyBytes_Concat(val1, val2);
        return true;
    }

#if PY_MAJOR_VERSION < 3
    if (PyString_Check(val1) && PyString_Check(val2)) {
        PyString_Concat(val1, val2);
        return true;
    }
#endif
    return false;
}

PyObject* fromFormat(const char* format, ...)
{
    va_list argp;
    va_start(argp, format);
    PyObject* result = 0;
#if PY_MAJOR_VERSION >= 3
    result = PyUnicode_FromFormatV(format, argp);
#else
    result = PyString_FromFormatV(format, argp);
#endif
    va_end(argp);
    return result;
}

PyObject* fromStringAndSize(const char* str, Py_ssize_t size)
{
#if PY_MAJOR_VERSION >= 3
    return PyUnicode_FromStringAndSize(str, size);
#else
    return PyString_FromStringAndSize(str, size);
#endif
}

int compare(PyObject* val1, const char* val2)
{
    if (PyUnicode_Check(val1))
#if PY_MAJOR_VERSION >= 3
       return PyUnicode_CompareWithASCIIString(val1, val2);
#else
    {
        PyObject* uVal2 = PyUnicode_FromFormat("%s", val2);
        bool result =  PyUnicode_Compare(val1, uVal2);
        Py_XDECREF(uVal2);
        return result;
    }
    if (PyString_Check(val1))
        return strcmp(PyString_AS_STRING(val1), val2);
#endif
    return 0;

}

Py_ssize_t len(PyObject* str)
{
    if (str == Py_None)
        return 0;

    if (PyUnicode_Check(str))
        return PyUnicode_GET_SIZE(str);

    if (PyBytes_Check(str))
        return PyBytes_GET_SIZE(str);

#if PY_MAJOR_VERSION < 3
    if (PyString_Check(str))
        return PyString_GET_SIZE(str);
#endif

    return 0;
}

} // namespace String

} // namespace Shiboken
