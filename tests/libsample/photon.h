/*
 * This file is part of the Shiboken Python Binding Generator project.
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

#ifndef PHOTON_H
#define PHOTON_H

#include "libsamplemacros.h"

// This namespace and classes simulate
// situations found in Qt's phonon module.

namespace Photon
{

enum ClassType {
    IdentityType = 1,
    DuplicatorType = 2
};

template<ClassType CLASS_TYPE>
class LIBSAMPLE_API TemplateBase
{
public:
    explicit TemplateBase(int value) : m_value(value) {}
    inline void setValue(int value) { m_value = value; }
    inline int value() const { return m_value; }
    inline int multiplicator() const { return (int)CLASS_TYPE; }
    inline int calculate() const { return m_value * ((int)CLASS_TYPE); }
    static inline ClassType classType() { return CLASS_TYPE; }

    inline int sumValueUsingPointer(TemplateBase<CLASS_TYPE>* other) const { return m_value + other->m_value; }
    inline int sumValueUsingReference(TemplateBase<CLASS_TYPE>& other) const { return m_value + other.m_value; }

    static inline TemplateBase<CLASS_TYPE>* passPointerThrough(TemplateBase<CLASS_TYPE>* obj) { return obj; }
private:
    TemplateBase(const TemplateBase&);
    TemplateBase& operator=(const TemplateBase&);
    int m_value;
};

typedef TemplateBase<IdentityType>   ValueIdentity;
typedef TemplateBase<DuplicatorType> ValueDuplicator;
LIBSAMPLE_API int callCalculateForValueDuplicatorPointer(ValueDuplicator* value);
LIBSAMPLE_API int callCalculateForValueDuplicatorReference(ValueDuplicator& value);

} // namespace Photon

#endif // PHOTON_H
