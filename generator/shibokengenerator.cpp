/*
 * This file is part of the Shiboken Python Bindings Generator project.
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: PySide team <contact@pyside.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "shibokengenerator.h"
#include "overloaddata.h"
#include <reporthandler.h>
#include <typedatabase.h>

#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <limits>
#include <memory>

#define NULL_VALUE "NULL"
#define AVOID_PROTECTED_HACK "avoid-protected-hack"
#define PARENT_CTOR_HEURISTIC "enable-parent-ctor-heuristic"
#define RETURN_VALUE_HEURISTIC "enable-return-value-heuristic"
#define ENABLE_PYSIDE_EXTENSIONS "enable-pyside-extensions"
#define DISABLE_VERBOSE_ERROR_MESSAGES "disable-verbose-error-messages"
#define USE_ISNULL_AS_NB_NONZERO "use-isnull-as-nb_nonzero"

//static void dumpFunction(AbstractMetaFunctionList lst);
static QString baseConversionString(QString typeName);

QHash<QString, QString> ShibokenGenerator::m_pythonPrimitiveTypeName = QHash<QString, QString>();
QHash<QString, QString> ShibokenGenerator::m_pythonOperators = QHash<QString, QString>();
QHash<QString, QString> ShibokenGenerator::m_formatUnits = QHash<QString, QString>();
QHash<QString, QString> ShibokenGenerator::m_tpFuncs = QHash<QString, QString>();
QStringList ShibokenGenerator::m_knownPythonTypes = QStringList();

static QString resolveScopePrefix(const AbstractMetaClass* scope, const QString& value)
{
    if (!scope)
        return QString();

    QString name;
    QStringList parts = scope->qualifiedCppName().split("::", QString::SkipEmptyParts);
    for(int i = (parts.size() - 1) ; i >= 0; i--) {
        if (!value.startsWith(parts[i] + "::"))
            name = parts[i] + "::" + name;
        else
            name = "";
    }

    return name;
}
ShibokenGenerator::ShibokenGenerator() : Generator()
{
    if (m_pythonPrimitiveTypeName.isEmpty())
        ShibokenGenerator::initPrimitiveTypesCorrespondences();

    if (m_tpFuncs.isEmpty())
        ShibokenGenerator::clearTpFuncs();

    if (m_knownPythonTypes.isEmpty())
        ShibokenGenerator::initKnownPythonTypes();

    m_metaTypeFromStringCache = AbstractMetaTypeCache();

    m_typeSystemConvName[TypeSystemCheckFunction]         = "checkType";
    m_typeSystemConvName[TypeSystemIsConvertibleFunction] = "isConvertible";
    m_typeSystemConvName[TypeSystemToCppFunction]         = "toCpp";
    m_typeSystemConvName[TypeSystemToPythonFunction]      = "toPython";
    m_typeSystemConvRegEx[TypeSystemCheckFunction]         = QRegExp("%CHECKTYPE\\[([^\\[]*)\\]");
    m_typeSystemConvRegEx[TypeSystemIsConvertibleFunction] = QRegExp("%ISCONVERTIBLE\\[([^\\[]*)\\]");
    m_typeSystemConvRegEx[TypeSystemToCppFunction]         = QRegExp("%CONVERTTOCPP\\[([^\\[]*)\\]");
    m_typeSystemConvRegEx[TypeSystemToPythonFunction]      = QRegExp("%CONVERTTOPYTHON\\[([^\\[]*)\\]");
}

ShibokenGenerator::~ShibokenGenerator()
{
    qDeleteAll(m_metaTypeFromStringCache.values());
}

void ShibokenGenerator::clearTpFuncs()
{
    m_tpFuncs["__str__"] = QString("0");
    m_tpFuncs["__repr__"] = QString("0");
    m_tpFuncs["__iter__"] = QString("0");
    m_tpFuncs["__next__"] = QString("0");
}

void ShibokenGenerator::initPrimitiveTypesCorrespondences()
{
    // Python primitive types names
    m_pythonPrimitiveTypeName.clear();

    // PyBool
    m_pythonPrimitiveTypeName["bool"] = "PyBool";

    // PyInt
    m_pythonPrimitiveTypeName["char"] = "SbkChar";
    m_pythonPrimitiveTypeName["signed char"] = "SbkChar";
    m_pythonPrimitiveTypeName["unsigned char"] = "SbkChar";
    m_pythonPrimitiveTypeName["int"] = "PyInt";
    m_pythonPrimitiveTypeName["signed int"] = "PyInt";
    m_pythonPrimitiveTypeName["uint"] = "PyInt";
    m_pythonPrimitiveTypeName["unsigned int"] = "PyInt";
    m_pythonPrimitiveTypeName["short"] = "PyInt";
    m_pythonPrimitiveTypeName["ushort"] = "PyInt";
    m_pythonPrimitiveTypeName["signed short"] = "PyInt";
    m_pythonPrimitiveTypeName["unsigned short"] = "PyInt";
    m_pythonPrimitiveTypeName["long"] = "PyInt";

    // PyFloat
    m_pythonPrimitiveTypeName["double"] = "PyFloat";
    m_pythonPrimitiveTypeName["float"] = "PyFloat";

    // PyLong
    m_pythonPrimitiveTypeName["unsigned long"] = "PyLong";
    m_pythonPrimitiveTypeName["signed long"] = "PyLong";
    m_pythonPrimitiveTypeName["ulong"] = "PyLong";
    m_pythonPrimitiveTypeName["long long"] = "PyLong";
    m_pythonPrimitiveTypeName["__int64"] = "PyLong";
    m_pythonPrimitiveTypeName["unsigned long long"] = "PyLong";
    m_pythonPrimitiveTypeName["unsigned __int64"] = "PyLong";

    // Python operators
    m_pythonOperators.clear();

    // call operator
    m_pythonOperators["operator()"] = "call";

    // Arithmetic operators
    m_pythonOperators["operator+"] = "add";
    m_pythonOperators["operator-"] = "sub";
    m_pythonOperators["operator*"] = "mul";
    m_pythonOperators["operator/"] = "div";
    m_pythonOperators["operator%"] = "mod";

    // Inplace arithmetic operators
    m_pythonOperators["operator+="] = "iadd";
    m_pythonOperators["operator-="] = "isub";
    m_pythonOperators["operator++"] = "iadd";
    m_pythonOperators["operator--"] = "isub";
    m_pythonOperators["operator*="] = "imul";
    m_pythonOperators["operator/="] = "idiv";
    m_pythonOperators["operator%="] = "imod";

    // Bitwise operators
    m_pythonOperators["operator&"] = "and";
    m_pythonOperators["operator^"] = "xor";
    m_pythonOperators["operator|"] = "or";
    m_pythonOperators["operator<<"] = "lshift";
    m_pythonOperators["operator>>"] = "rshift";
    m_pythonOperators["operator~"] = "invert";

    // Inplace bitwise operators
    m_pythonOperators["operator&="] = "iand";
    m_pythonOperators["operator^="] = "ixor";
    m_pythonOperators["operator|="] = "ior";
    m_pythonOperators["operator<<="] = "ilshift";
    m_pythonOperators["operator>>="] = "irshift";

    // Comparison operators
    m_pythonOperators["operator=="] = "eq";
    m_pythonOperators["operator!="] = "ne";
    m_pythonOperators["operator<"] = "lt";
    m_pythonOperators["operator>"] = "gt";
    m_pythonOperators["operator<="] = "le";
    m_pythonOperators["operator>="] = "ge";

    // Initialize format units for C++->Python->C++ conversion
    m_formatUnits.clear();
    m_formatUnits.insert("char", "b");
    m_formatUnits.insert("unsigned char", "B");
    m_formatUnits.insert("int", "i");
    m_formatUnits.insert("unsigned int", "I");
    m_formatUnits.insert("short", "h");
    m_formatUnits.insert("unsigned short", "H");
    m_formatUnits.insert("long", "l");
    m_formatUnits.insert("unsigned long", "k");
    m_formatUnits.insert("long long", "L");
    m_formatUnits.insert("__int64", "L");
    m_formatUnits.insert("unsigned long long", "K");
    m_formatUnits.insert("unsigned __int64", "K");
    m_formatUnits.insert("double", "d");
    m_formatUnits.insert("float", "f");
}

void ShibokenGenerator::initKnownPythonTypes()
{
    m_knownPythonTypes.clear();
    m_knownPythonTypes << "PyBool" << "PyInt" << "PyFloat" << "PyLong";
    m_knownPythonTypes << "PyObject" << "PyString" << "PyBuffer";
    m_knownPythonTypes << "PySequence" << "PyTuple" << "PyList" << "PyDict";
    m_knownPythonTypes << "PyObject*" << "PyObject *" << "PyTupleObject*";
}

QString ShibokenGenerator::translateTypeForWrapperMethod(const AbstractMetaType* cType,
                                                         const AbstractMetaClass* context,
                                                         Options options) const
{
    if (cType->isArray())
        return translateTypeForWrapperMethod(cType->arrayElementType(), context, options) + "[]";

    if (avoidProtectedHack() && cType->isEnum()) {
        const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(cType);
        if (metaEnum && metaEnum->isProtected())
            return protectedEnumSurrogateName(metaEnum);
    }

    return translateType(cType, context, options);
}

bool ShibokenGenerator::shouldGenerateCppWrapper(const AbstractMetaClass* metaClass) const
{
    bool result = metaClass->isPolymorphic() || metaClass->hasVirtualDestructor();
    if (avoidProtectedHack()) {
        result = result || metaClass->hasProtectedFields() || metaClass->hasProtectedDestructor();
        if (!result && metaClass->hasProtectedFunctions()) {
            int protectedFunctions = 0;
            int protectedOperators = 0;
            foreach (const AbstractMetaFunction* func, metaClass->functions()) {
                if (!func->isProtected() || func->isSignal() || func->isModifiedRemoved())
                    continue;
                else if (func->isOperatorOverload())
                    protectedOperators++;
                else
                    protectedFunctions++;
            }
            result = result || (protectedFunctions > protectedOperators);
        }
    } else {
        result = result && !metaClass->hasPrivateDestructor();
    }
    return result && !metaClass->isNamespace();
}

void ShibokenGenerator::lookForEnumsInClassesNotToBeGenerated(AbstractMetaEnumList& enumList, const AbstractMetaClass* metaClass)
{
    if (!metaClass)
        return;

    if (metaClass->typeEntry()->codeGeneration() == TypeEntry::GenerateForSubclass) {
        foreach (const AbstractMetaEnum* metaEnum, metaClass->enums()) {
            if (metaEnum->isPrivate() || metaEnum->typeEntry()->codeGeneration() == TypeEntry::GenerateForSubclass)
                continue;
            if (!enumList.contains(const_cast<AbstractMetaEnum*>(metaEnum)))
                enumList.append(const_cast<AbstractMetaEnum*>(metaEnum));
        }
        lookForEnumsInClassesNotToBeGenerated(enumList, metaClass->enclosingClass());
    }
}

static const AbstractMetaClass* getProperEnclosingClass(const AbstractMetaClass* metaClass)
{
    if (!metaClass)
        return 0;

    if (metaClass->typeEntry()->codeGeneration() != TypeEntry::GenerateForSubclass)
        return metaClass;

    return getProperEnclosingClass(metaClass->enclosingClass());
}

const AbstractMetaClass* ShibokenGenerator::getProperEnclosingClassForEnum(const AbstractMetaEnum* metaEnum)
{
    return getProperEnclosingClass(metaEnum->enclosingClass());
}

QString ShibokenGenerator::wrapperName(const AbstractMetaClass* metaClass) const
{
    if (shouldGenerateCppWrapper(metaClass)) {
        QString result = metaClass->name();
        if (metaClass->enclosingClass()) // is a inner class
            result.replace("::", "_");

        result +="Wrapper";
        return result;
    } else {
        return metaClass->qualifiedCppName();
    }
}

QString ShibokenGenerator::fullPythonFunctionName(const AbstractMetaFunction* func)
{
    QString funcName;
    if (func->isOperatorOverload())
        funcName = ShibokenGenerator::pythonOperatorFunctionName(func);
    else
       funcName = func->name();
    if (func->ownerClass()) {
        QString fullName = func->ownerClass()->fullName();
        if (func->isConstructor())
            funcName = fullName;
        else
            funcName.prepend(fullName + '.');
    }
    return funcName;
}

QString ShibokenGenerator::protectedEnumSurrogateName(const AbstractMetaEnum* metaEnum)
{
    return metaEnum->fullName().replace(".", "_") + "_Surrogate";
}

QString ShibokenGenerator::protectedFieldGetterName(const AbstractMetaField* field)
{
    return QString("protected_%1_getter").arg(field->name());
}

QString ShibokenGenerator::protectedFieldSetterName(const AbstractMetaField* field)
{
    return QString("protected_%1_setter").arg(field->name());
}

QString ShibokenGenerator::cpythonFunctionName(const AbstractMetaFunction* func)
{
    QString result;

    if (func->ownerClass()) {
        result = cpythonBaseName(func->ownerClass()->typeEntry());
        if (func->isConstructor() || func->isCopyConstructor()) {
            result += "_Init";
        } else {
            result += "Func_";
            if (func->isOperatorOverload())
                result += ShibokenGenerator::pythonOperatorFunctionName(func);
            else
                result += func->name();
        }
    } else {
        result = "Sbk" + moduleName() + "Module_" + func->name();
    }

    return result;
}

QString ShibokenGenerator::cpythonMethodDefinitionName(const AbstractMetaFunction* func)
{
    if (!func->ownerClass())
        return QString();
    return QString("%1Method_%2").arg(cpythonBaseName(func->ownerClass()->typeEntry())).arg(func->name());
}

QString ShibokenGenerator::cpythonGettersSettersDefinitionName(const AbstractMetaClass* metaClass)
{
    return QString("%1_getsetlist").arg(cpythonBaseName(metaClass));
}

QString ShibokenGenerator::cpythonSetattroFunctionName(const AbstractMetaClass* metaClass)
{
    return QString("%1_setattro").arg(cpythonBaseName(metaClass));
}


QString ShibokenGenerator::cpythonGetattroFunctionName(const AbstractMetaClass* metaClass)
{
    return QString("%1_getattro").arg(cpythonBaseName(metaClass));
}

QString ShibokenGenerator::cpythonGetterFunctionName(const AbstractMetaField* metaField)
{
    return QString("%1_get_%2").arg(cpythonBaseName(metaField->enclosingClass())).arg(metaField->name());
}

QString ShibokenGenerator::cpythonSetterFunctionName(const AbstractMetaField* metaField)
{
    return QString("%1_set_%2").arg(cpythonBaseName(metaField->enclosingClass())).arg(metaField->name());
}

static QString cpythonEnumFlagsName(QString moduleName, QString qualifiedCppName)
{
    QString result = QString("Sbk%1_%2").arg(moduleName).arg(qualifiedCppName);
    result.replace("::", "_");
    return result;
}

static QString searchForEnumScope(const AbstractMetaClass* metaClass, const QString& value)
{
    QString enumValueName = value.trimmed();

    if (!metaClass)
        return QString();

    foreach (const AbstractMetaEnum* metaEnum, metaClass->enums()) {
        foreach (const AbstractMetaEnumValue* enumValue, metaEnum->values()) {
            if (enumValueName == enumValue->name())
                return metaClass->qualifiedCppName();
        }
    }

    return searchForEnumScope(metaClass->enclosingClass(), enumValueName);
}

/*
 * This function uses some heuristics to find out the scope for a given
 * argument default value. New situations may arise in the future and
 * this method should be updated, do it with care.
 */
QString ShibokenGenerator::guessScopeForDefaultValue(const AbstractMetaFunction* func, const AbstractMetaArgument* arg)
{
    QString value = getDefaultValue(func, arg);
    if (value.isEmpty())
        return QString();

    static QRegExp enumValueRegEx("^([A-Za-z_]\\w*)?$");
    QString prefix;
    QString suffix;

    if (arg->type()->isEnum()) {
        const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(arg->type());
        if (metaEnum)
            prefix = resolveScopePrefix(metaEnum->enclosingClass(), value);
    } else if (arg->type()->isFlags()) {
        static QRegExp numberRegEx("^\\d+$"); // Numbers to flags
        if (numberRegEx.exactMatch(value)) {
            QString typeName = translateTypeForWrapperMethod(arg->type(), func->implementingClass());
            if (arg->type()->isConstant())
                typeName.remove(0, sizeof("const ") / sizeof(char) - 1);
            if (arg->type()->isReference())
                typeName.chop(1);
            prefix = typeName + '(';
            suffix = ')';
        }

        static QRegExp enumCombinationRegEx("^([A-Za-z_][\\w:]*)\\(([^,\\(\\)]*)\\)$"); // FlagName(EnumItem|EnumItem|...)
        if (prefix.isEmpty() && enumCombinationRegEx.indexIn(value) != -1) {
            QString flagName = enumCombinationRegEx.cap(1);
            QStringList enumItems = enumCombinationRegEx.cap(2).split("|");
            QString scope = searchForEnumScope(func->implementingClass(), enumItems.first());
            if (!scope.isEmpty())
                scope.append("::");

            QStringList fixedEnumItems;
            foreach (const QString& enumItem, enumItems)
                fixedEnumItems << QString(scope + enumItem);

            if (!fixedEnumItems.isEmpty()) {
                prefix = flagName + '(';
                value = fixedEnumItems.join("|");
                suffix = ')';
            }
        }
    } else if (arg->type()->typeEntry()->isValue()) {
        const AbstractMetaClass* metaClass = classes().findClass(arg->type()->typeEntry());
        if (enumValueRegEx.exactMatch(value))
            prefix = resolveScopePrefix(metaClass, value);
    } else if (arg->type()->isPrimitive() && arg->type()->name() == "int") {
        if (enumValueRegEx.exactMatch(value) && func->implementingClass())
            prefix = resolveScopePrefix(func->implementingClass(), value);
    } else if(arg->type()->isPrimitive()) {
        static QRegExp unknowArgumentRegEx("^(?:[A-Za-z_][\\w:]*\\()?([A-Za-z_]\\w*)(?:\\))?$"); // [PrimitiveType(] DESIREDNAME [)]
        if (unknowArgumentRegEx.indexIn(value) != -1 && func->implementingClass()) {
            foreach (const AbstractMetaField* field, func->implementingClass()->fields()) {
                if (unknowArgumentRegEx.cap(1).trimmed() == field->name()) {
                    QString fieldName = field->name();
                    if (field->isStatic()) {
                        prefix = resolveScopePrefix(func->implementingClass(), value);
                        fieldName.prepend(prefix);
                        prefix= "";
                    } else {
                        fieldName.prepend(CPP_SELF_VAR "->");
                    }
                    value.replace(unknowArgumentRegEx.cap(1), fieldName);
                    break;
                }
            }
        }
    }

    if (!prefix.isEmpty())
        value.prepend(prefix);
    if (!suffix.isEmpty())
        value.append(suffix);

    return value;
}

QString ShibokenGenerator::cpythonEnumName(const EnumTypeEntry* enumEntry)
{
    return cpythonEnumFlagsName(enumEntry->targetLangPackage().replace(".", "_"), enumEntry->qualifiedCppName());
}

QString ShibokenGenerator::cpythonFlagsName(const FlagsTypeEntry* flagsEntry)
{
    return cpythonEnumFlagsName(flagsEntry->targetLangPackage().replace(".", "_"), flagsEntry->originalName());
}

QString ShibokenGenerator::cpythonSpecialCastFunctionName(const AbstractMetaClass* metaClass)
{
    return cpythonBaseName(metaClass->typeEntry())+"SpecialCastFunction";
}

QString ShibokenGenerator::cpythonWrapperCPtr(const AbstractMetaClass* metaClass, QString argName)
{
    return cpythonWrapperCPtr(metaClass->typeEntry(), argName);
}

QString ShibokenGenerator::cpythonWrapperCPtr(const AbstractMetaType* metaType, QString argName)
{
    return cpythonWrapperCPtr(metaType->typeEntry(), argName);
}

QString ShibokenGenerator::cpythonWrapperCPtr(const TypeEntry* type, QString argName)
{
    if (ShibokenGenerator::isWrapperType(type))
        return baseConversionString( "::" + type->qualifiedCppName() + '*') + "toCpp(" + argName + ')';
    return QString();
}

QString ShibokenGenerator::getFunctionReturnType(const AbstractMetaFunction* func, Options options) const
{
    if (func->ownerClass() && (func->isConstructor() || func->isCopyConstructor()))
        return func->ownerClass()->qualifiedCppName() + '*';

    return translateTypeForWrapperMethod(func->type(), func->implementingClass());

    //TODO: check these lines
    //QString modifiedReturnType = QString(func->typeReplaced(0));
    //return modifiedReturnType.isNull() ?
    //translateType(func->type(), func->implementingClass()) : modifiedReturnType;
}

static QString baseConversionString(QString typeName)
{
    return QString("Shiboken::Converter< %1 >::").arg(typeName);
}

void ShibokenGenerator::writeBaseConversion(QTextStream& s, const TypeEntry* type)
{
    QString typeName;

    if (avoidProtectedHack() && type->isEnum()) {
        const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(type);
        if (metaEnum && metaEnum->isProtected())
            typeName = protectedEnumSurrogateName(metaEnum);
    } else {
        typeName = getFullTypeName(type).trimmed();
        if (isObjectType(type))
            typeName.append('*');
    }
    s << baseConversionString(typeName);
}

void ShibokenGenerator::writeBaseConversion(QTextStream& s, const AbstractMetaType* type,
                                            const AbstractMetaClass* context, Options options)
{
    QString typeName;
    if (type->isPrimitive()) {
        const PrimitiveTypeEntry* ptype = (const PrimitiveTypeEntry*) type->typeEntry();
        if (ptype->basicAliasedTypeEntry())
            ptype = ptype->basicAliasedTypeEntry();
        typeName = ptype->name();
        if (!ptype->isCppPrimitive())
            typeName.prepend("::");
    } else {
        if (!isCString(type)) {
            options |= Generator::ExcludeConst;
            if (type->typeEntry()->isPrimitive()
                || type->isContainer()
                || type->isFlags()
                || type->isEnum()
                || (type->isConstant() && type->isReference())) {
                options |= Generator::ExcludeReference;
            }
        }
        typeName = translateTypeForWrapperMethod(type, context, options).trimmed();
    }

    s << baseConversionString(typeName);
}

void ShibokenGenerator::writeToPythonConversion(QTextStream& s, const AbstractMetaType* type,
                                                const AbstractMetaClass* context, const QString& argumentName)
{
    s << cpythonToPythonConversionFunction(type, context) << '(' << argumentName << ')';
}

void ShibokenGenerator::writeToCppConversion(QTextStream& s, const AbstractMetaClass* metaClass,
                                             const QString& argumentName)
{
    s << cpythonToCppConversionFunction(metaClass) << '(' << argumentName << ')';
}

void ShibokenGenerator::writeToCppConversion(QTextStream& s, const AbstractMetaType* type,
                                             const AbstractMetaClass* context, const QString& argumentName)
{
    s << cpythonToCppConversionFunction(type, context) << '(' << argumentName << ')';
}

bool ShibokenGenerator::shouldRejectNullPointerArgument(const AbstractMetaFunction* func, int argIndex)
{
    if (argIndex < 0 || argIndex >= func->arguments().count())
        return false;
    // Argument type is not a pointer, a None rejection should not be
    // necessary because the type checking would handle that already.
    if (!isPointer(func->arguments().at(argIndex)->type()))
        return false;
    if (func->argumentRemoved(argIndex + 1))
        return false;
    foreach (FunctionModification funcMod, func->modifications()) {
        foreach (ArgumentModification argMod, funcMod.argument_mods) {
            if (argMod.index == argIndex + 1 && argMod.noNullPointers)
                return true;
        }
    }
    return false;
}

QString ShibokenGenerator::getFormatUnitString(const AbstractMetaFunction* func, bool incRef) const
{
    QString result;
    const char objType = (incRef ? 'O' : 'N');
    foreach (const AbstractMetaArgument* arg, func->arguments()) {
        if (func->argumentRemoved(arg->argumentIndex() + 1))
            continue;

        if (!func->typeReplaced(arg->argumentIndex() + 1).isEmpty()) {
            result += objType;
        } else if (arg->type()->isQObject()
            || arg->type()->isObject()
            || arg->type()->isValue()
            || arg->type()->isValuePointer()
            || arg->type()->isNativePointer()
            || arg->type()->isEnum()
            || arg->type()->isFlags()
            || arg->type()->isContainer()
            || arg->type()->isReference()) {
            result += objType;
        } else if (arg->type()->isPrimitive()) {
            const PrimitiveTypeEntry* ptype = (const PrimitiveTypeEntry*) arg->type()->typeEntry();
            if (ptype->basicAliasedTypeEntry())
                ptype = ptype->basicAliasedTypeEntry();
            if (m_formatUnits.contains(ptype->name()))
                result += m_formatUnits[ptype->name()];
            else
                result += objType;
        } else if (isCString(arg->type())) {
            result += 'z';
        } else {
            QString report;
            QTextStream(&report) << "Method: " << func->ownerClass()->qualifiedCppName()
                                 << "::" << func->signature() << " => Arg:"
                                 << arg->name() << "index: " << arg->argumentIndex()
                                 << " - cannot be handled properly. Use an inject-code to fix it!";
            ReportHandler::warning(report);
            result += '?';
        }
    }
    return result;
}

QString ShibokenGenerator::cpythonBaseName(const AbstractMetaType* type)
{
    if (isCString(type))
        return QString("PyString");
    return cpythonBaseName(type->typeEntry());
}

QString ShibokenGenerator::cpythonBaseName(const AbstractMetaClass* metaClass)
{
    return cpythonBaseName(metaClass->typeEntry());
}

QString ShibokenGenerator::cpythonBaseName(const TypeEntry* type)
{
    QString baseName;
    if (ShibokenGenerator::isWrapperType(type) || type->isNamespace()) { // && !type->isReference()) {
        baseName = "Sbk_" + type->name();
    } else if (type->isPrimitive()) {
        const PrimitiveTypeEntry* ptype = (const PrimitiveTypeEntry*) type;
        while (ptype->basicAliasedTypeEntry())
            ptype = ptype->basicAliasedTypeEntry();
        if (ptype->targetLangApiName() == ptype->name())
            baseName = m_pythonPrimitiveTypeName[ptype->name()];
        else
            baseName = ptype->targetLangApiName();
    } else if (type->isEnum()) {
        baseName = cpythonEnumName((const EnumTypeEntry*) type);
    } else if (type->isFlags()) {
        baseName = cpythonFlagsName((const FlagsTypeEntry*) type);
    } else if (type->isContainer()) {
        const ContainerTypeEntry* ctype = (const ContainerTypeEntry*) type;
        switch (ctype->type()) {
            case ContainerTypeEntry::ListContainer:
            case ContainerTypeEntry::StringListContainer:
            case ContainerTypeEntry::LinkedListContainer:
            case ContainerTypeEntry::VectorContainer:
            case ContainerTypeEntry::StackContainer:
            case ContainerTypeEntry::QueueContainer:
                //baseName = "PyList";
                //break;
            case ContainerTypeEntry::PairContainer:
                //baseName = "PyTuple";
                baseName = "PySequence";
                break;
            case ContainerTypeEntry::SetContainer:
                baseName = "PySet";
                break;
            case ContainerTypeEntry::MapContainer:
            case ContainerTypeEntry::MultiMapContainer:
            case ContainerTypeEntry::HashContainer:
            case ContainerTypeEntry::MultiHashContainer:
                baseName = "PyDict";
                break;
            default:
                Q_ASSERT(false);
        }
    } else {
        baseName = "PyObject";
    }
    return baseName.replace("::", "_");
}

QString ShibokenGenerator::cpythonTypeName(const AbstractMetaClass* metaClass)
{
    return cpythonTypeName(metaClass->typeEntry());
}

QString ShibokenGenerator::cpythonTypeName(const TypeEntry* type)
{
    return cpythonBaseName(type) + "_Type";
}

QString ShibokenGenerator::cpythonTypeNameExt(const TypeEntry* type)
{
    return cppApiVariableName(type->targetLangPackage()) + '[' + getTypeIndexVariableName(type) + ']';
}

QString ShibokenGenerator::cpythonTypeNameExt(const AbstractMetaType* type)
{
    return cppApiVariableName(type->typeEntry()->targetLangPackage()) + '[' + getTypeIndexVariableName(type) + ']';
}

QString ShibokenGenerator::cpythonOperatorFunctionName(const AbstractMetaFunction* func)
{
    if (!func->isOperatorOverload())
        return QString();
    return QString("Sbk") + func->ownerClass()->name()
            + '_' + pythonOperatorFunctionName(func->originalName());
}

QString ShibokenGenerator::pythonPrimitiveTypeName(const QString& cppTypeName)
{
    return ShibokenGenerator::m_pythonPrimitiveTypeName.value(cppTypeName, QString());
}

QString ShibokenGenerator::pythonPrimitiveTypeName(const PrimitiveTypeEntry* type)
{
    while (type->basicAliasedTypeEntry())
        type = type->basicAliasedTypeEntry();
    return pythonPrimitiveTypeName(type->name());
}

QString ShibokenGenerator::pythonOperatorFunctionName(QString cppOpFuncName)
{
    QString value = m_pythonOperators.value(cppOpFuncName);
    if (value.isEmpty()) {
        ReportHandler::warning("Unknown operator: "+cppOpFuncName);
        value = "UNKNOWN_OPERATOR";
    }
    value.prepend("__").append("__");
    return value;
}

QString ShibokenGenerator::pythonOperatorFunctionName(const AbstractMetaFunction* func)
{
    QString op = pythonOperatorFunctionName(func->originalName());
    if (func->arguments().isEmpty()) {
        if (op == "__sub__")
            op = QString("__neg__");
        else if (op == "__add__")
            op = QString("__pos__");
    } else if (func->isStatic() && func->arguments().size() == 2) {
        // If a operator overload function has 2 arguments and
        // is static we assume that it is a reverse operator.
        op = op.insert(2, 'r');
    }
    return op;
}

QString ShibokenGenerator::pythonRichCompareOperatorId(QString cppOpFuncName)
{
    return QString("Py_%1").arg(m_pythonOperators.value(cppOpFuncName).toUpper());
}

QString ShibokenGenerator::pythonRichCompareOperatorId(const AbstractMetaFunction* func)
{
    return pythonRichCompareOperatorId(func->originalName());
}

bool ShibokenGenerator::isNumber(QString cpythonApiName)
{
    return cpythonApiName == "PyInt"
            || cpythonApiName == "PyFloat"
            || cpythonApiName == "PyLong"
            || cpythonApiName == "PyBool";
}

bool ShibokenGenerator::isNumber(const TypeEntry* type)
{
    if (!type->isPrimitive())
        return false;
    return isNumber(pythonPrimitiveTypeName((const PrimitiveTypeEntry*) type));
}

bool ShibokenGenerator::isNumber(const AbstractMetaType* type)
{
    return isNumber(type->typeEntry());
}

bool ShibokenGenerator::isPyInt(const TypeEntry* type)
{
    if (!type->isPrimitive())
        return false;
    return pythonPrimitiveTypeName((const PrimitiveTypeEntry*) type) == "PyInt";
}

bool ShibokenGenerator::isPyInt(const AbstractMetaType* type)
{
    return isPyInt(type->typeEntry());
}

bool ShibokenGenerator::isCString(const AbstractMetaType* type)
{
    return type->isNativePointer()
            && type->indirections() == 1
            && type->name() == "char";
}

bool ShibokenGenerator::isPairContainer(const AbstractMetaType* type)
{
    return type->isContainer()
            && ((ContainerTypeEntry*)type->typeEntry())->type() == ContainerTypeEntry::PairContainer;
}

bool ShibokenGenerator::isWrapperType(const TypeEntry* type)
{
    if (type->isComplex())
        return ShibokenGenerator::isWrapperType((const ComplexTypeEntry*)type);
    return type->isObject() || type->isValue();
}
bool ShibokenGenerator::isWrapperType(const ComplexTypeEntry* type)
{
    return isObjectType(type) || type->isValue();
}
bool ShibokenGenerator::isWrapperType(const AbstractMetaType* metaType)
{
    return isObjectType(metaType)
            || metaType->typeEntry()->isValue();
}

bool ShibokenGenerator::isPointerToWrapperType(const AbstractMetaType* type)
{
    return isObjectType(type) || type->isValuePointer();
}

bool ShibokenGenerator::shouldDereferenceArgumentPointer(const AbstractMetaArgument* arg)
{
    return shouldDereferenceAbstractMetaTypePointer(arg->type());
}

bool ShibokenGenerator::shouldDereferenceAbstractMetaTypePointer(const AbstractMetaType* metaType)
{
    return isWrapperType(metaType) && !isPointer(metaType)
            && (metaType->isValue() || metaType->isReference());
}

bool ShibokenGenerator::visibilityModifiedToPrivate(const AbstractMetaFunction* func)
{
    foreach (FunctionModification mod, func->modifications()) {
        if (mod.modifiers & Modification::Private)
            return true;
    }
    return false;
}

QString ShibokenGenerator::cpythonCheckFunction(const AbstractMetaType* metaType, bool genericNumberType)
{
    QString customCheck;
    if (metaType->typeEntry()->isCustom()) {
        AbstractMetaType* type;
        customCheck = guessCPythonCheckFunction(metaType->typeEntry()->name(), &type);
        if (type)
            metaType = type;
        if (!customCheck.isEmpty())
            return customCheck;
    }

    QString baseName = cpythonBaseName(metaType);
    if (isNumber(baseName))
        return genericNumberType ? QString("SbkNumber_Check") : QString("%1_Check").arg(baseName);


    baseName.clear();
    QTextStream b(&baseName);
    // exclude const on Objects
    Options flags = getConverterOptions(metaType);
    writeBaseConversion(b, metaType, 0, flags);

    return QString("%1checkType").arg(baseName);
}

QString ShibokenGenerator::cpythonCheckFunction(const TypeEntry* type, bool genericNumberType)
{
    QString customCheck;
    if (type->isCustom()) {
        AbstractMetaType* metaType;
        customCheck = guessCPythonCheckFunction(type->name(), &metaType);
        if (metaType)
            return cpythonCheckFunction(metaType, genericNumberType);
        return customCheck;
    }

    QString baseName = cpythonBaseName(type);
    if (isNumber(baseName))
        return genericNumberType ? "SbkNumber_Check" : baseName+"_Check";

    baseName.clear();
    QTextStream b(&baseName);
    writeBaseConversion(b, type);
    return QString("%1checkType").arg(baseName);
}

QString ShibokenGenerator::guessCPythonCheckFunction(const QString& type, AbstractMetaType** metaType)
{
    *metaType = 0;
    if (type == "PyTypeObject")
        return "PyType_Check";

    if (type == "PyBuffer")
        return "Shiboken::Buffer::checkType";

    if (type == "str")
        return "Shiboken::String::check";

    *metaType = buildAbstractMetaTypeFromString(type);
    if (*metaType && !(*metaType)->typeEntry()->isCustom())
        return QString();

    return QString("%1_Check").arg(type);
}

QString ShibokenGenerator::guessCPythonIsConvertible(const QString& type)
{
    if (type == "PyTypeObject")
        return "PyType_Check";

    AbstractMetaType* metaType = buildAbstractMetaTypeFromString(type);
    if (metaType && !metaType->typeEntry()->isCustom())
        return cpythonIsConvertibleFunction(metaType);

    return QString("%1_Check").arg(type);
}

QString ShibokenGenerator::cpythonIsConvertibleFunction(const TypeEntry* type, bool genericNumberType, bool checkExact)
{
    if (checkExact)
        return cpythonCheckFunction(type, genericNumberType);

    if (type->isCustom())
        return guessCPythonIsConvertible(type->name());

    QString baseName;
    QTextStream b(&baseName);
    writeBaseConversion(b, type);
    return QString("%1isConvertible").arg(baseName);
}
QString ShibokenGenerator::cpythonIsConvertibleFunction(const AbstractMetaType* metaType, bool genericNumberType)
{
    QString customCheck;
    if (metaType->typeEntry()->isCustom()) {
        AbstractMetaType* type;
        customCheck = guessCPythonCheckFunction(metaType->typeEntry()->name(), &type);
        if (type)
            metaType = type;
        if (!customCheck.isEmpty())
            return customCheck;
    }

    QString baseName = cpythonBaseName(metaType);
    if (isNumber(baseName))
        return genericNumberType ? QString("SbkNumber_Check") : QString("%1_Check").arg(baseName);

    baseName.clear();
    QTextStream b(&baseName);
    writeBaseConversion(b, metaType);
    return QString("%1isConvertible").arg(baseName);
}

QString ShibokenGenerator::cpythonToCppConversionFunction(const AbstractMetaClass* metaClass)
{
    QString base;
    QTextStream b(&base);
    writeBaseConversion(b, metaClass->typeEntry());
    return QString("%1toCpp").arg(base);
}
QString ShibokenGenerator::cpythonToCppConversionFunction(const AbstractMetaType* type, const AbstractMetaClass* context)
{
    QString base;
    QTextStream b(&base);
    writeBaseConversion(b, type, context);
    return QString("%1toCpp").arg(base);
}

QString ShibokenGenerator::cpythonToPythonConversionFunction(const AbstractMetaType* type, const AbstractMetaClass* context)
{
    // exclude const on Objects
    Options flags = getConverterOptions(type);
    QString base;
    QTextStream b(&base);
    writeBaseConversion(b, type, context, flags);
    return QString("%1toPython").arg(base);
}

QString ShibokenGenerator::cpythonToPythonConversionFunction(const AbstractMetaClass* metaClass)
{
    return cpythonToPythonConversionFunction(metaClass->typeEntry());
}

QString ShibokenGenerator::cpythonToPythonConversionFunction(const TypeEntry* type)
{
    QString base;
    QTextStream b(&base);
    writeBaseConversion(b, type);
    return QString("%1toPython").arg(base);
}

QString ShibokenGenerator::argumentString(const AbstractMetaFunction *func,
                                          const AbstractMetaArgument *argument,
                                          Options options) const
{
    QString modified_type;
    if (!(options & OriginalTypeDescription))
        modified_type = func->typeReplaced(argument->argumentIndex() + 1);
    QString arg;

    if (modified_type.isEmpty())
        arg = translateType(argument->type(), func->implementingClass(), options);
    else
        arg = modified_type.replace('$', '.');

    if (!(options & Generator::SkipName)) {
        arg += " ";
        arg += argument->name();
    }

    QList<ReferenceCount> referenceCounts;
    referenceCounts = func->referenceCounts(func->implementingClass(), argument->argumentIndex() + 1);
    if ((options & Generator::SkipDefaultValues) != Generator::SkipDefaultValues &&
        !argument->originalDefaultValueExpression().isEmpty())
    {
        QString default_value = argument->originalDefaultValueExpression();
        if (default_value == "NULL")
            default_value = NULL_VALUE;

        //WORKAROUND: fix this please
        if (default_value.startsWith("new "))
            default_value.remove(0, 4);

        arg += " = " + default_value;
    }

    return arg;
}

void ShibokenGenerator::writeArgument(QTextStream &s,
                                      const AbstractMetaFunction *func,
                                      const AbstractMetaArgument *argument,
                                      Options options) const
{
    s << argumentString(func, argument, options);
}

void ShibokenGenerator::writeFunctionArguments(QTextStream &s,
                                               const AbstractMetaFunction *func,
                                               Options options) const
{
    AbstractMetaArgumentList arguments = func->arguments();

    if (options & Generator::WriteSelf) {
        s << func->implementingClass()->name() << '&';
        if (!(options & SkipName))
            s << " " PYTHON_SELF_VAR;
    }

    int argUsed = 0;
    for (int i = 0; i < arguments.size(); ++i) {
        if ((options & Generator::SkipRemovedArguments) && func->argumentRemoved(i+1))
            continue;

        if ((options & Generator::WriteSelf) || argUsed != 0)
            s << ", ";
        writeArgument(s, func, arguments[i], options);
        argUsed++;
    }
}

QString ShibokenGenerator::functionReturnType(const AbstractMetaFunction* func, Options options) const
{
    QString modifiedReturnType = QString(func->typeReplaced(0));
    if (!modifiedReturnType.isNull() && !(options & OriginalTypeDescription))
        return modifiedReturnType;
    else
        return translateType(func->type(), func->implementingClass(), options);
}

QString ShibokenGenerator::functionSignature(const AbstractMetaFunction *func,
                                             QString prepend,
                                             QString append,
                                             Options options,
                                             int argCount) const
{
    QString result;
    QTextStream s(&result);
    // The actual function
    if (!(func->isEmptyFunction() ||
          func->isNormal() ||
          func->isSignal())) {
        options |= Generator::SkipReturnType;
    } else {
        s << functionReturnType(func, options) << ' ';
    }

    // name
    QString name(func->originalName());
    if (func->isConstructor())
        name = wrapperName(func->ownerClass());

    s << prepend << name << append << '(';
    writeFunctionArguments(s, func, options);
    s << ')';

    if (func->isConstant() && !(options & Generator::ExcludeMethodConst))
        s << " const";

    return result;
}

void ShibokenGenerator::writeArgumentNames(QTextStream &s,
                                           const AbstractMetaFunction *func,
                                           Options options) const
{
    AbstractMetaArgumentList arguments = func->arguments();
    int argCount = 0;
    for (int j = 0, max = arguments.size(); j < max; j++) {

        if ((options & Generator::SkipRemovedArguments) && (func->argumentRemoved(arguments.at(j)->argumentIndex()+1)))
            continue;

        s << ((argCount > 0) ? ", " : "") << arguments.at(j)->name();

        if (((options & Generator::VirtualCall) == 0)
            && (!func->conversionRule(TypeSystem::NativeCode, arguments.at(j)->argumentIndex() + 1).isEmpty()
                || !func->conversionRule(TypeSystem::TargetLangCode, arguments.at(j)->argumentIndex() + 1).isEmpty())
            && !func->isConstructor()) {
           s << CONV_RULE_OUT_VAR_SUFFIX;
        }

        argCount++;
    }
}

void ShibokenGenerator::writeFunctionCall(QTextStream& s,
                                          const AbstractMetaFunction* func,
                                          Options options) const
{
    if (!(options & Generator::SkipName))
        s << (func->isConstructor() ? func->ownerClass()->qualifiedCppName() : func->originalName());
    s << '(';
    writeArgumentNames(s, func, options);
    s << ')';
}

void ShibokenGenerator::writeUnusedVariableCast(QTextStream& s, const QString& variableName)
{
    s << INDENT << "SBK_UNUSED(" << variableName<< ')' << endl;
}

AbstractMetaFunctionList ShibokenGenerator::filterFunctions(const AbstractMetaClass* metaClass)
{
    AbstractMetaFunctionList result;
    foreach (AbstractMetaFunction *func, metaClass->functions()) {
        //skip signals
        if (func->isSignal() || func->isDestructor()
            || (func->isModifiedRemoved() && !func->isAbstract()
                && (!avoidProtectedHack() || !func->isProtected())))
            continue;
        result << func;
    }
    return result;
}

ShibokenGenerator::ExtendedConverterData ShibokenGenerator::getExtendedConverters() const
{
    ExtendedConverterData extConvs;
    foreach (const AbstractMetaClass* metaClass, classes()) {
        // Use only the classes for the current module.
        if (!shouldGenerate(metaClass))
            continue;
        foreach (AbstractMetaFunction* convOp, metaClass->operatorOverloads(AbstractMetaClass::ConversionOp)) {
            // Get only the conversion operators that return a type from another module,
            // that are value-types and were not removed in the type system.
            const TypeEntry* convType = convOp->type()->typeEntry();
            if ((convType->codeGeneration() & TypeEntry::GenerateTargetLang)
                || !convType->isValue()
                || convOp->isModifiedRemoved())
                continue;
            extConvs[convType].append(convOp->ownerClass());
        }
    }
    return extConvs;
}

static QString getArgumentsFromMethodCall(const QString& str)
{
    // It would be way nicer to be able to use a Perl like
    // regular expression that accepts temporary variables
    // to count the parenthesis.
    // For more information check this:
    // http://perl.plover.com/yak/regex/samples/slide083.html
    static QString funcCall("%CPPSELF.%FUNCTION_NAME");
    int pos = str.indexOf(funcCall);
    if (pos == -1)
        return QString();
    pos = pos + funcCall.count();
    while (str.at(pos) == ' ' || str.at(pos) == '\t')
        ++pos;
    if (str.at(pos) == '(')
        ++pos;
    int begin = pos;
    int counter = 1;
    while (counter != 0) {
        if (str.at(pos) == '(')
            ++counter;
        else if (str.at(pos) == ')')
            --counter;
        ++pos;
    }
    return str.mid(begin, pos-begin-1);
}

QString ShibokenGenerator::getCodeSnippets(const CodeSnipList& codeSnips,
                                           CodeSnip::Position position,
                                           TypeSystem::Language language)
{
    QString code;
    QTextStream c(&code);
    foreach (CodeSnip snip, codeSnips) {
        if ((position != CodeSnip::Any && snip.position != position) || !(snip.language & language))
            continue;
        QString snipCode;
        QTextStream sc(&snipCode);
        formatCode(sc, snip.code(), INDENT);
        c << snipCode;
    }
    return code;
}
void ShibokenGenerator::processCodeSnip(QString& code, const AbstractMetaClass* context)
{
    if (context) {
        // Replace template variable by the Python Type object
        // for the class context in which the variable is used.
        code.replace("%PYTHONTYPEOBJECT", cpythonTypeName(context) + ".super.ht_type");
        code.replace("%TYPE", wrapperName(context));
        code.replace("%CPPTYPE", context->name());
    }

    // replace "toPython" converters
    replaceConvertToPythonTypeSystemVariable(code);

    // replace "toCpp" converters
    replaceConvertToCppTypeSystemVariable(code);

    // replace "isConvertible" check
    replaceIsConvertibleToCppTypeSystemVariable(code);

    // replace "checkType" check
    replaceTypeCheckTypeSystemVariable(code);
}

ShibokenGenerator::ArgumentVarReplacementList ShibokenGenerator::getArgumentReplacement(const AbstractMetaFunction* func,
                                                                                        bool usePyArgs, TypeSystem::Language language,
                                                                                        const AbstractMetaArgument* lastArg)
{
    ArgumentVarReplacementList argReplacements;
    TypeSystem::Language convLang = (language == TypeSystem::TargetLangCode)
                                    ? TypeSystem::NativeCode : TypeSystem::TargetLangCode;
    int removed = 0;
    for (int i = 0; i < func->arguments().size(); ++i) {
        const AbstractMetaArgument* arg = func->arguments().at(i);
        QString argValue;
        if (language == TypeSystem::TargetLangCode) {
            bool hasConversionRule = !func->conversionRule(convLang, i+1).isEmpty();
            bool argRemoved = func->argumentRemoved(i+1);
            removed = removed + (int) argRemoved;
            if (argRemoved && hasConversionRule)
                argValue = QString("%1"CONV_RULE_OUT_VAR_SUFFIX).arg(arg->name());
            else if (argRemoved || (lastArg && arg->argumentIndex() > lastArg->argumentIndex()))
                argValue = QString(CPP_ARG_REMOVED"%1").arg(i);
            if (!argRemoved && argValue.isEmpty()) {
                int argPos = i - removed;
                if (arg->type()->typeEntry()->isCustom()) {
                    argValue = usePyArgs ? QString(PYTHON_ARGS"[%1]").arg(argPos) : PYTHON_ARG;
                } else {
                    argValue = hasConversionRule
                               ? QString("%1"CONV_RULE_OUT_VAR_SUFFIX).arg(arg->name())
                               : QString(CPP_ARG"%1").arg(argPos);
                }
            }
        } else {
            argValue = arg->name();
        }
        if (!argValue.isEmpty())
            argReplacements << ArgumentVarReplacementPair(arg, argValue);

    }
    return argReplacements;
}

void ShibokenGenerator::writeCodeSnips(QTextStream& s,
                                       const CodeSnipList& codeSnips,
                                       CodeSnip::Position position,
                                       TypeSystem::Language language,
                                       const AbstractMetaClass* context)
{
    QString code = getCodeSnippets(codeSnips, position, language);
    if (code.isEmpty())
        return;
    processCodeSnip(code, context);
    s << INDENT << "// Begin code injection" << endl;
    s << code;
    s << INDENT << "// End of code injection" << endl;
}

void ShibokenGenerator::writeCodeSnips(QTextStream& s,
                                       const CodeSnipList& codeSnips,
                                       CodeSnip::Position position,
                                       TypeSystem::Language language,
                                       const AbstractMetaFunction* func,
                                       const AbstractMetaArgument* lastArg)
{
    QString code = getCodeSnippets(codeSnips, position, language);
    if (code.isEmpty())
        return;

    // Calculate the real number of arguments.
    int argsRemoved = 0;
    for (int i = 0; i < func->arguments().size(); i++) {
        if (func->argumentRemoved(i+1))
            argsRemoved++;
    }

    OverloadData od(getFunctionGroups(func->implementingClass())[func->name()], this);
    bool usePyArgs = pythonFunctionWrapperUsesListOfArguments(od);

    // Replace %PYARG_# variables.
    code.replace("%PYARG_0", PYTHON_RETURN_VAR);

    static QRegExp pyArgsRegex("%PYARG_(\\d+)");
    if (language == TypeSystem::TargetLangCode) {
        if (usePyArgs) {
            code.replace(pyArgsRegex, PYTHON_ARGS"[\\1-1]");
        } else {
            static QRegExp pyArgsRegexCheck("%PYARG_([2-9]+)");
            if (pyArgsRegexCheck.indexIn(code) != -1) {
                ReportHandler::warning("Wrong index for %PYARG variable ("+pyArgsRegexCheck.cap(1)+") on "+func->signature());
                return;
            }
            code.replace("%PYARG_1", PYTHON_ARG);
        }
    } else {
        // Replaces the simplest case of attribution to a
        // Python argument on the binding virtual method.
        static QRegExp pyArgsAttributionRegex("%PYARG_(\\d+)\\s*=[^=]\\s*([^;]+)");
        code.replace(pyArgsAttributionRegex, "PyTuple_SET_ITEM(" PYTHON_ARGS ", \\1-1, \\2)");
        code.replace(pyArgsRegex, "PyTuple_GET_ITEM(" PYTHON_ARGS ", \\1-1)");
    }

    // Replace %ARG#_TYPE variables.
    foreach (const AbstractMetaArgument* arg, func->arguments()) {
        QString argTypeVar = QString("%ARG%1_TYPE").arg(arg->argumentIndex() + 1);
        QString argTypeVal = arg->type()->cppSignature();
        code.replace(argTypeVar, argTypeVal);
    }

    int pos = 0;
    static QRegExp cppArgTypeRegexCheck("%ARG(\\d+)_TYPE");
    while ((pos = cppArgTypeRegexCheck.indexIn(code, pos)) != -1) {
        ReportHandler::warning("Wrong index for %ARG#_TYPE variable ("+cppArgTypeRegexCheck.cap(1)+") on "+func->signature());
        pos += cppArgTypeRegexCheck.matchedLength();
    }

    // Replace template variable for return variable name.
    if (func->isConstructor()) {
        code.replace("%0.", QString("%1->").arg("cptr"));
        code.replace("%0", "cptr");
    } else if (func->type()) {
        QString returnValueOp = isPointerToWrapperType(func->type()) ? "%1->" : "%1.";
        if (ShibokenGenerator::isWrapperType(func->type()))
            code.replace("%0.", returnValueOp.arg(CPP_RETURN_VAR));
        code.replace("%0", CPP_RETURN_VAR);
    }

    // Replace template variable for self Python object.
    QString pySelf = (language == TypeSystem::NativeCode) ? "pySelf" : PYTHON_SELF_VAR;
    code.replace("%PYSELF", pySelf);

    // Replace template variable for a pointer to C++ of this object.
    if (func->implementingClass()) {
        QString replacement = func->isStatic() ? "%1::" : "%1->";
        QString cppSelf;
        if (func->isStatic())
            cppSelf = func->ownerClass()->qualifiedCppName();
        else if (language == TypeSystem::NativeCode)
            cppSelf = "this";
        else
            cppSelf = CPP_SELF_VAR;

        // On comparison operator CPP_SELF_VAR is always a reference.
        if (func->isComparisonOperator())
            replacement = "%1.";

        if (func->isVirtual() && !func->isAbstract() && (!avoidProtectedHack() || !func->isProtected())) {
            QString methodCallArgs = getArgumentsFromMethodCall(code);
            if (!methodCallArgs.isNull()) {
                if (func->name() == "metaObject") {
                    QString wrapperClassName = wrapperName(func->ownerClass());
                    QString cppSelfVar = avoidProtectedHack() ? QString("%CPPSELF") : QString("reinterpret_cast<%1*>(%CPPSELF)").arg(wrapperClassName);
                    code.replace(QString("%CPPSELF.%FUNCTION_NAME(%1)").arg(methodCallArgs),
                                 QString("(Shiboken::Object::hasCppWrapper(reinterpret_cast<SbkObject*>(%1))"
                                         " ? %2->::%3::%FUNCTION_NAME(%4)"
                                         " : %CPPSELF.%FUNCTION_NAME(%4))").arg(pySelf).arg(cppSelfVar).arg(wrapperClassName).arg(methodCallArgs));
                } else {
                    code.replace(QString("%CPPSELF.%FUNCTION_NAME(%1)").arg(methodCallArgs),
                                 QString("(Shiboken::Object::hasCppWrapper(reinterpret_cast<SbkObject*>(%1))"
                                         " ? %CPPSELF->::%TYPE::%FUNCTION_NAME(%2)"
                                         " : %CPPSELF.%FUNCTION_NAME(%2))").arg(pySelf).arg(methodCallArgs));
                }
            }
        }

        code.replace("%CPPSELF.", replacement.arg(cppSelf));
        code.replace("%CPPSELF", cppSelf);

        if (code.indexOf("%BEGIN_ALLOW_THREADS") > -1) {
            if (code.count("%BEGIN_ALLOW_THREADS") == code.count("%END_ALLOW_THREADS")) {
                code.replace("%BEGIN_ALLOW_THREADS", BEGIN_ALLOW_THREADS);
                code.replace("%END_ALLOW_THREADS", END_ALLOW_THREADS);
            } else {
                ReportHandler::warning("%BEGIN_ALLOW_THREADS and %END_ALLOW_THREADS mismatch");
            }
        }

        // replace template variable for the Python Type object for the
        // class implementing the method in which the code snip is written
        if (func->isStatic()) {
            code.replace("%PYTHONTYPEOBJECT", cpythonTypeName(func->implementingClass()) + ".super.ht_type");
        } else {
            code.replace("%PYTHONTYPEOBJECT.", QString("%1->ob_type->").arg(pySelf));
            code.replace("%PYTHONTYPEOBJECT", QString("%1->ob_type").arg(pySelf));
        }
    }

    // Replaces template %ARGUMENT_NAMES and %# variables by argument variables and values.
    // Replaces template variables %# for individual arguments.
    ArgumentVarReplacementList argReplacements = getArgumentReplacement(func, usePyArgs, language, lastArg);

    QStringList args;
    foreach (ArgumentVarReplacementPair pair, argReplacements) {
        if (pair.second.startsWith(CPP_ARG_REMOVED))
            continue;
        args << pair.second;
    }
    code.replace("%ARGUMENT_NAMES", args.join(", "));

    foreach (ArgumentVarReplacementPair pair, argReplacements) {
        const AbstractMetaArgument* arg = pair.first;
        int idx = arg->argumentIndex() + 1;
        QString replacement = pair.second;
        if (isWrapperType(arg->type()) && isPointer(arg->type()))
            code.replace(QString("%%1.").arg(idx), QString("%1->").arg(replacement));
        code.replace(QString("%%1").arg(idx), replacement);
    }

    if (language == TypeSystem::NativeCode) {
        // Replaces template %PYTHON_ARGUMENTS variable with a pointer to the Python tuple
        // containing the converted virtual method arguments received from C++ to be passed
        // to the Python override.
        code.replace("%PYTHON_ARGUMENTS", PYTHON_ARGS);

        // replace variable %PYTHON_METHOD_OVERRIDE for a pointer to the Python method
        // override for the C++ virtual method in which this piece of code was inserted
        code.replace("%PYTHON_METHOD_OVERRIDE", PYTHON_OVERRIDE_VAR);
    }

    if (avoidProtectedHack()) {
        // If the function being processed was added by the user via type system,
        // Shiboken needs to find out if there are other overloads for the same method
        // name and if any of them is of the protected visibility. This is used to replace
        // calls to %FUNCTION_NAME on user written custom code for calls to the protected
        // dispatcher.
        bool hasProtectedOverload = false;
        if (func->isUserAdded()) {
            foreach (const AbstractMetaFunction* f, getFunctionOverloads(func->ownerClass(), func->name()))
                hasProtectedOverload |= f->isProtected();
        }

        if (func->isProtected() || hasProtectedOverload) {
            code.replace("%TYPE::%FUNCTION_NAME",
                         QString("%1::%2_protected")
                         .arg(wrapperName(func->ownerClass()))
                         .arg(func->originalName()));
            code.replace("%FUNCTION_NAME", QString("%1_protected").arg(func->originalName()));
        }
    }

    if (func->isConstructor() && shouldGenerateCppWrapper(func->ownerClass()))
        code.replace("%TYPE", wrapperName(func->ownerClass()));

    if (func->ownerClass())
        code.replace("%CPPTYPE", func->ownerClass()->name());

    replaceTemplateVariables(code, func);

    processCodeSnip(code);
    s << INDENT << "// Begin code injection" << endl;
    s << code;
    s << INDENT << "// End of code injection" << endl;
}

typedef QPair<QString, QString> StringPair;
void ShibokenGenerator::replaceConverterTypeSystemVariable(TypeSystemConverterVariable converterVariable, QString& code)
{
    QRegExp& regex = m_typeSystemConvRegEx[converterVariable];
    const QString& conversionName = m_typeSystemConvName[converterVariable];
    int pos = 0;
    QList<StringPair> replacements;
    while ((pos = regex.indexIn(code, pos)) != -1) {
        pos += regex.matchedLength();
        QStringList list = regex.capturedTexts();
        QString conversionVar = list.first();
        QString conversionTypeName = list.last();
        const AbstractMetaType* conversionType = buildAbstractMetaTypeFromString(conversionTypeName);
        QString conversion;
        if (conversionType) {
            switch (converterVariable) {
                case TypeSystemCheckFunction:
                    conversion = cpythonCheckFunction(conversionType);
                    break;
                case TypeSystemIsConvertibleFunction:
                    conversion = cpythonIsConvertibleFunction(conversionType);
                    break;
                case TypeSystemToCppFunction:
                    conversion = cpythonToCppConversionFunction(conversionType);
                    break;
                case TypeSystemToPythonFunction:
                    conversion = cpythonToPythonConversionFunction(conversionType);
                    break;
                default:
                    Q_ASSERT(false);
            }
        } else {
            conversion = QString("Shiboken::Converter< %1 >::%2").arg(conversionTypeName).arg(conversionName);
        }
        replacements.append(qMakePair(conversionVar, conversion));
    }
    foreach (StringPair rep, replacements)
        code.replace(rep.first, rep.second);
}

bool ShibokenGenerator::injectedCodeUsesCppSelf(const AbstractMetaFunction* func)
{
    CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, TypeSystem::TargetLangCode);
    foreach (CodeSnip snip, snips) {
        if (snip.code().contains("%CPPSELF"))
            return true;
    }
    return false;
}

bool ShibokenGenerator::injectedCodeUsesPySelf(const AbstractMetaFunction* func)
{
    CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, TypeSystem::NativeCode);
    foreach (CodeSnip snip, snips) {
        if (snip.code().contains("%PYSELF"))
            return true;
    }
    return false;
}

bool ShibokenGenerator::injectedCodeCallsCppFunction(const AbstractMetaFunction* func)
{
    QString funcCall = QString("%1(").arg(func->originalName());
    QString wrappedCtorCall;
    if (func->isConstructor()) {
        funcCall.prepend("new ");
        wrappedCtorCall = QString("new %1(").arg(wrapperName(func->ownerClass()));
    }
    CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, TypeSystem::TargetLangCode);
    foreach (CodeSnip snip, snips) {
        if (snip.code().contains("%FUNCTION_NAME(") || snip.code().contains(funcCall)
            || (func->isConstructor()
                && ((func->ownerClass()->isPolymorphic() && snip.code().contains(wrappedCtorCall))
                    || snip.code().contains("new %TYPE(")))
            )
            return true;
    }
    return false;
}

bool ShibokenGenerator::injectedCodeCallsPythonOverride(const AbstractMetaFunction* func)
{
    static QRegExp overrideCallRegexCheck("PyObject_Call\\s*\\(\\s*%PYTHON_METHOD_OVERRIDE\\s*,");
    CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, TypeSystem::NativeCode);
    foreach (CodeSnip snip, snips) {
        if (overrideCallRegexCheck.indexIn(snip.code()) != -1)
            return true;
    }
    return false;
}

bool ShibokenGenerator::injectedCodeHasReturnValueAttribution(const AbstractMetaFunction* func, TypeSystem::Language language)
{
    static QRegExp retValAttributionRegexCheck_native("%0\\s*=[^=]\\s*.+");
    static QRegExp retValAttributionRegexCheck_target("%PYARG_0\\s*=[^=]\\s*.+");
    CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, language);
    foreach (CodeSnip snip, snips) {
        if (language == TypeSystem::TargetLangCode) {
            if (retValAttributionRegexCheck_target.indexIn(snip.code()) != -1)
                return true;
        } else {
            if (retValAttributionRegexCheck_native.indexIn(snip.code()) != -1)
                return true;
        }
    }
    return false;
}

bool ShibokenGenerator::injectedCodeUsesArgument(const AbstractMetaFunction* func, int argumentIndex)
{
    CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any);
    foreach (CodeSnip snip, snips) {
        QString code = snip.code();
        if (code.contains("%ARGUMENT_NAMES"))
            return true;
        if (code.contains(QString("%%1").arg(argumentIndex + 1)))
            return true;
    }
    return false;
}

bool ShibokenGenerator::hasMultipleInheritanceInAncestry(const AbstractMetaClass* metaClass)
{
    if (!metaClass || metaClass->baseClassNames().isEmpty())
        return false;
    if (metaClass->baseClassNames().size() > 1)
        return true;
    return hasMultipleInheritanceInAncestry(metaClass->baseClass());
}

bool ShibokenGenerator::classNeedsGetattroFunction(const AbstractMetaClass* metaClass)
{
    if (!metaClass)
        return false;
    foreach (AbstractMetaFunctionList allOverloads, getFunctionGroups(metaClass).values()) {
        AbstractMetaFunctionList overloads;
        foreach (AbstractMetaFunction* func, allOverloads) {
            if (func->isAssignmentOperator() || func->isCastOperator() || func->isModifiedRemoved()
                || func->isPrivate() || func->ownerClass() != func->implementingClass()
                || func->isConstructor() || func->isOperatorOverload())
                continue;
            overloads.append(func);
        }
        if (overloads.isEmpty())
            continue;
        if (OverloadData::hasStaticAndInstanceFunctions(overloads))
            return true;
    }
    return false;
}

AbstractMetaFunctionList ShibokenGenerator::getMethodsWithBothStaticAndNonStaticMethods(const AbstractMetaClass* metaClass)
{
    AbstractMetaFunctionList methods;
    if (metaClass) {
        foreach (AbstractMetaFunctionList allOverloads, getFunctionGroups(metaClass).values()) {
            AbstractMetaFunctionList overloads;
            foreach (AbstractMetaFunction* func, allOverloads) {
                if (func->isAssignmentOperator() || func->isCastOperator() || func->isModifiedRemoved()
                    || func->isPrivate() || func->ownerClass() != func->implementingClass()
                    || func->isConstructor() || func->isOperatorOverload())
                    continue;
                overloads.append(func);
            }
            if (overloads.isEmpty())
                continue;
            if (OverloadData::hasStaticAndInstanceFunctions(overloads))
                methods.append(overloads.first());
        }
    }
    return methods;
}

AbstractMetaClassList ShibokenGenerator::getBaseClasses(const AbstractMetaClass* metaClass) const
{
    AbstractMetaClassList baseClasses;
    if (metaClass) {
        foreach (QString parent, metaClass->baseClassNames()) {
            AbstractMetaClass* clazz = classes().findClass(parent);
            if (clazz)
                baseClasses << clazz;
        }
    }
    return baseClasses;
}

const AbstractMetaClass* ShibokenGenerator::getMultipleInheritingClass(const AbstractMetaClass* metaClass)
{
    if (!metaClass || metaClass->baseClassNames().isEmpty())
        return 0;
    if (metaClass->baseClassNames().size() > 1)
        return metaClass;
    return getMultipleInheritingClass(metaClass->baseClass());
}

AbstractMetaClassList ShibokenGenerator::getAllAncestors(const AbstractMetaClass* metaClass) const
{
    AbstractMetaClassList result;
    if (metaClass) {
        AbstractMetaClassList baseClasses = getBaseClasses(metaClass);
        foreach (AbstractMetaClass* base, baseClasses) {
            result.append(base);
            result.append(getAllAncestors(base));
        }
    }
    return result;
}

QString ShibokenGenerator::getModuleHeaderFileName(const QString& moduleName) const
{
    QString result = moduleName.isEmpty() ? packageName() : moduleName;
    result.replace(".", "_");
    return QString("%1_python.h").arg(result.toLower());
}

QString ShibokenGenerator::extendedIsConvertibleFunctionName(const TypeEntry* targetType) const
{
    return QString("ExtendedIsConvertible_%1_%2").arg(targetType->targetLangPackage().replace('.', '_')).arg(targetType->name());
}

QString ShibokenGenerator::extendedToCppFunctionName(const TypeEntry* targetType) const
{
    return QString("ExtendedToCpp_%1_%2").arg(targetType->targetLangPackage().replace('.', '_')).arg(targetType->name());
}

bool ShibokenGenerator::isCopyable(const AbstractMetaClass *metaClass)

{
    if (metaClass->isNamespace() || isObjectType(metaClass))
        return false;
    else if (metaClass->typeEntry()->copyable() == ComplexTypeEntry::Unknown)
        return metaClass->hasCloneOperator();
    else
        return (metaClass->typeEntry()->copyable() == ComplexTypeEntry::CopyableSet);

    return false;
}

AbstractMetaType* ShibokenGenerator::buildAbstractMetaTypeFromString(QString typeSignature)
{
    typeSignature = typeSignature.trimmed();

    if (m_metaTypeFromStringCache.contains(typeSignature))
        return m_metaTypeFromStringCache.value(typeSignature);

    QString typeString = typeSignature;
    bool isConst = typeString.startsWith("const ");
    if (isConst)
        typeString.remove(0, sizeof("const ") / sizeof(char) - 1);

    int indirections = typeString.count("*");
    while (typeString.endsWith("*")) {
        typeString.chop(1);
        typeString = typeString.trimmed();
    }

    bool isReference = typeString.endsWith("&");
    if (isReference) {
        typeString.chop(1);
        typeString = typeString.trimmed();
    }

    TypeEntry* typeEntry = TypeDatabase::instance()->findType(typeString);
    AbstractMetaType* metaType = 0;
    if (typeEntry) {
        metaType = new AbstractMetaType();
        metaType->setTypeEntry(typeEntry);
        metaType->setIndirections(indirections);
        metaType->setReference(isReference);
        metaType->setConstant(isConst);
        metaType->decideUsagePattern();
        m_metaTypeFromStringCache.insert(typeSignature, metaType);
    }
    return metaType;
}

/*
static void dumpFunction(AbstractMetaFunctionList lst)
{
    qDebug() << "DUMP FUNCTIONS: ";
    foreach (AbstractMetaFunction *func, lst)
        qDebug() << "*" << func->ownerClass()->name()
                        << func->signature()
                        << "Private: " << func->isPrivate()
                        << "Empty: " << func->isEmptyFunction()
                        << "Static:" << func->isStatic()
                        << "Signal:" << func->isSignal()
                        << "ClassImplements: " <<  (func->ownerClass() != func->implementingClass())
                        << "is operator:" << func->isOperatorOverload()
                        << "is global:" << func->isInGlobalScope();
}
*/

static bool isGroupable(const AbstractMetaFunction* func)
{
    if (func->isSignal() || func->isDestructor() || (func->isModifiedRemoved() && !func->isAbstract()))
        return false;
    // weird operator overloads
    if (func->name() == "operator[]" || func->name() == "operator->")  // FIXME: what about cast operators?
        return false;;
    return true;
}

QMap< QString, AbstractMetaFunctionList > ShibokenGenerator::getFunctionGroups(const AbstractMetaClass* scope)
{
    AbstractMetaFunctionList lst = scope ? scope->functions() : globalFunctions();

    QMap<QString, AbstractMetaFunctionList> results;
    foreach (AbstractMetaFunction* func, lst) {
        if (isGroupable(func))
            results[func->name()].append(func);
    }
    return results;
}

AbstractMetaFunctionList ShibokenGenerator::getFunctionOverloads(const AbstractMetaClass* scope, const QString& functionName)
{
    AbstractMetaFunctionList lst = scope ? scope->functions() : globalFunctions();

    AbstractMetaFunctionList results;
    foreach (AbstractMetaFunction* func, lst) {
        if (func->name() != functionName)
            continue;
        if (isGroupable(func))
            results << func;
    }
    return results;

}

QPair< int, int > ShibokenGenerator::getMinMaxArguments(const AbstractMetaFunction* metaFunction)
{
    AbstractMetaFunctionList overloads = getFunctionOverloads(metaFunction->ownerClass(), metaFunction->name());

    int minArgs = std::numeric_limits<int>::max();
    int maxArgs = 0;
    foreach (const AbstractMetaFunction* func, overloads) {
        int numArgs = 0;
        foreach (const AbstractMetaArgument* arg, func->arguments()) {
            if (!func->argumentRemoved(arg->argumentIndex() + 1))
                numArgs++;
        }
        maxArgs = std::max(maxArgs, numArgs);
        minArgs = std::min(minArgs, numArgs);
    }
    return qMakePair(minArgs, maxArgs);
}

QMap<QString, QString> ShibokenGenerator::options() const
{
    QMap<QString, QString> opts(Generator::options());
    opts.insert(AVOID_PROTECTED_HACK, "Avoid the use of the '#define protected public' hack.");
    opts.insert(PARENT_CTOR_HEURISTIC, "Enable heuristics to detect parent relationship on constructors.");
    opts.insert(RETURN_VALUE_HEURISTIC, "Enable heuristics to detect parent relationship on return values (USE WITH CAUTION!)");
    opts.insert(ENABLE_PYSIDE_EXTENSIONS, "Enable PySide extensions, such as support for signal/slots, use this if you are creating a binding for a Qt-based library.");
    opts.insert(DISABLE_VERBOSE_ERROR_MESSAGES, "Disable verbose error messages. Turn the python code hard to debug but safe few kB on the generated bindings.");
    opts.insert(USE_ISNULL_AS_NB_NONZERO, "If a class have an isNull()const method, it will be used to compute the value of boolean casts");
    return opts;
}

bool ShibokenGenerator::doSetup(const QMap<QString, QString>& args)
{
    m_useCtorHeuristic = args.contains(PARENT_CTOR_HEURISTIC);
    m_usePySideExtensions = args.contains(ENABLE_PYSIDE_EXTENSIONS);
    m_userReturnValueHeuristic = args.contains(RETURN_VALUE_HEURISTIC);
    m_verboseErrorMessagesDisabled = args.contains(DISABLE_VERBOSE_ERROR_MESSAGES);
    m_useIsNullAsNbNonZero = args.contains(USE_ISNULL_AS_NB_NONZERO);
    m_avoidProtectedHack = args.contains(AVOID_PROTECTED_HACK);
    return true;
}

bool ShibokenGenerator::useCtorHeuristic() const
{
    return m_useCtorHeuristic;
}

bool ShibokenGenerator::useReturnValueHeuristic() const
{
    return m_userReturnValueHeuristic;
}

bool ShibokenGenerator::usePySideExtensions() const
{
    return m_usePySideExtensions;
}

bool ShibokenGenerator::useIsNullAsNbNonZero() const
{
    return m_useIsNullAsNbNonZero;
}

bool ShibokenGenerator::avoidProtectedHack() const
{
    return m_avoidProtectedHack;
}

QString ShibokenGenerator::cppApiVariableName(const QString& moduleName) const
{
    QString result = moduleName.isEmpty() ? ShibokenGenerator::packageName() : moduleName;
    result.replace(".", "_");
    result.prepend("Sbk");
    result.append("Types");
    return result;
}

static QString _fixedCppTypeName(QString typeName)
{
    return typeName.replace(" ",  "")
                   .replace(".",  "_")
                   .replace("<",  "_")
                   .replace(">",  "_")
                   .replace("::", "_")
                   .replace("*",  "PTR")
                   .replace("&",  "REF");
}

static QString processInstantiationsVariableName(const AbstractMetaType* type)
{
    QString res = QString("_%1").arg(_fixedCppTypeName(type->typeEntry()->qualifiedCppName()).toUpper());
    foreach (const AbstractMetaType* instantiation, type->instantiations())
        res += processInstantiationsVariableName(instantiation);
    return res;
}
QString ShibokenGenerator::getTypeIndexVariableName(const AbstractMetaClass* metaClass, bool alternativeTemplateName)
{
    if (alternativeTemplateName) {
        const AbstractMetaClass* templateBaseClass = metaClass->templateBaseClass();
        if (!templateBaseClass)
            return QString();
        QString base = _fixedCppTypeName(templateBaseClass->typeEntry()->qualifiedCppName()).toUpper();
        QString instantiations;
        foreach (const AbstractMetaType* instantiation, metaClass->templateBaseClassInstantiations())
            instantiations += processInstantiationsVariableName(instantiation);
        return QString("SBK_%1%2_IDX").arg(base).arg(instantiations);
    }
    return getTypeIndexVariableName(metaClass->typeEntry());
}
QString ShibokenGenerator::getTypeIndexVariableName(const TypeEntry* type)
{
    return QString("SBK_%1_IDX").arg(_fixedCppTypeName(type->qualifiedCppName()).toUpper());
}
QString ShibokenGenerator::getTypeIndexVariableName(const AbstractMetaType* type)
{
    return QString("SBK%1_IDX").arg(processInstantiationsVariableName(type));
}

QString ShibokenGenerator::getFullTypeName(const TypeEntry* type)
{
    return QString("%1%2").arg(type->isCppPrimitive() ? "" : "::").arg(type->qualifiedCppName());
}
QString ShibokenGenerator::getFullTypeName(const AbstractMetaType* type)
{
    if (isCString(type))
        return QString("const char*");
    return getFullTypeName(type->typeEntry()) + QString("*").repeated(type->indirections());
}
QString ShibokenGenerator::getFullTypeName(const AbstractMetaClass* metaClass)
{
    return getFullTypeName(metaClass->typeEntry());
}

bool ShibokenGenerator::verboseErrorMessagesDisabled() const
{
    return m_verboseErrorMessagesDisabled;
}

bool ShibokenGenerator::pythonFunctionWrapperUsesListOfArguments(const OverloadData& overloadData)
{
    if (overloadData.referenceFunction()->isCallOperator())
        return true;
    if (overloadData.referenceFunction()->isOperatorOverload())
        return false;
    int maxArgs = overloadData.maxArgs();
    int minArgs = overloadData.minArgs();
    return (minArgs != maxArgs)
           || (maxArgs > 1)
           || overloadData.referenceFunction()->isConstructor()
           || overloadData.hasArgumentWithDefaultValue();
}

Generator::Options ShibokenGenerator::getConverterOptions(const AbstractMetaType* metaType)
{
    // exclude const on Objects
    Options flags;
    const TypeEntry* type = metaType->typeEntry();
    bool isCStr = isCString(metaType);
    if (metaType->indirections() && !isCStr) {
        flags = ExcludeConst;
    } else if (metaType->isContainer()
        || (type->isPrimitive() && !isCStr)
        // const refs become just the value, but pure refs must remain pure.
        || (type->isValue() && metaType->isConstant() && metaType->isReference())) {
        flags = ExcludeConst | ExcludeReference;
    }
    return flags;
}

QString  ShibokenGenerator::getDefaultValue(const AbstractMetaFunction* func, const AbstractMetaArgument* arg)
{
    if (!arg->defaultValueExpression().isEmpty())
        return arg->defaultValueExpression();

    //Check modifications
    foreach(FunctionModification  m, func->modifications()) {
        foreach(ArgumentModification am, m.argument_mods) {
            if (am.index == (arg->argumentIndex() + 1))
                return am.replacedDefaultExpression;
        }
    }
    return QString();
}
