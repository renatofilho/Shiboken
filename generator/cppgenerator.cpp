/*
 * This file is part of the Shiboken Python Bindings Generator project.
 *
 * Copyright (C) 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
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

#include <memory>

#include "cppgenerator.h"
#include "shibokennormalize_p.h"
#include <reporthandler.h>
#include <typedatabase.h>

#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include <QtCore/QDebug>
#include <QMetaType>

QHash<QString, QString> CppGenerator::m_nbFuncs = QHash<QString, QString>();
QHash<QString, QString> CppGenerator::m_sqFuncs = QHash<QString, QString>();
QHash<QString, QString> CppGenerator::m_mpFuncs = QHash<QString, QString>();
QString CppGenerator::m_currentErrorCode("0");

// utility functions
inline AbstractMetaType* getTypeWithoutContainer(AbstractMetaType* arg)
{
    if (arg && arg->typeEntry()->isContainer()) {
        AbstractMetaTypeList lst = arg->instantiations();
        // only support containers with 1 type
        if (lst.size() == 1)
            return lst[0];
    }
    return arg;
}

static QString reduceTypeName(const AbstractMetaClass* metaClass)
{
    QString qualifiedCppName = metaClass->typeEntry()->qualifiedCppName();
    QString lookupName =  metaClass->typeEntry()->lookupName();
    if (lookupName != qualifiedCppName)
        return lookupName;
    return QString();
}

CppGenerator::CppGenerator()
{
    // Number protocol structure members names
    m_nbFuncs["__add__"] = "nb_add";
    m_nbFuncs["__sub__"] = "nb_subtract";
    m_nbFuncs["__mul__"] = "nb_multiply";
    m_nbFuncs["__div__"] = "nb_divide";
    m_nbFuncs["__mod__"] = "nb_remainder";
    m_nbFuncs["__neg__"] = "nb_negative";
    m_nbFuncs["__pos__"] = "nb_positive";
    m_nbFuncs["__invert__"] = "nb_invert";
    m_nbFuncs["__lshift__"] = "nb_lshift";
    m_nbFuncs["__rshift__"] = "nb_rshift";
    m_nbFuncs["__and__"] = "nb_and";
    m_nbFuncs["__xor__"] = "nb_xor";
    m_nbFuncs["__or__"] = "nb_or";
    m_nbFuncs["__iadd__"] = "nb_inplace_add";
    m_nbFuncs["__isub__"] = "nb_inplace_subtract";
    m_nbFuncs["__imul__"] = "nb_multiply";
    m_nbFuncs["__idiv__"] = "nb_divide";
    m_nbFuncs["__imod__"] = "nb_remainder";
    m_nbFuncs["__ilshift__"] = "nb_inplace_lshift";
    m_nbFuncs["__irshift__"] = "nb_inplace_rshift";
    m_nbFuncs["__iand__"] = "nb_inplace_and";
    m_nbFuncs["__ixor__"] = "nb_inplace_xor";
    m_nbFuncs["__ior__"] = "nb_inplace_or";
    m_nbFuncs["bool"] = "nb_nonzero";

    // sequence protocol functions
    typedef QPair<QString, QString> StrPair;
    m_sequenceProtocol.insert("__len__", StrPair("PyObject* " PYTHON_SELF_VAR, "Py_ssize_t"));
    m_sequenceProtocol.insert("__getitem__", StrPair("PyObject* " PYTHON_SELF_VAR ", Py_ssize_t _i", "PyObject*"));
    m_sequenceProtocol.insert("__setitem__", StrPair("PyObject* " PYTHON_SELF_VAR ", Py_ssize_t _i, PyObject* _value", "int"));
    m_sequenceProtocol.insert("__getslice__", StrPair("PyObject* " PYTHON_SELF_VAR ", Py_ssize_t _i1, Py_ssize_t _i2", "PyObject*"));
    m_sequenceProtocol.insert("__setslice__", StrPair("PyObject* " PYTHON_SELF_VAR ", Py_ssize_t _i1, Py_ssize_t _i2, PyObject* _value", "int"));
    m_sequenceProtocol.insert("__contains__", StrPair("PyObject* " PYTHON_SELF_VAR ", PyObject* _value", "int"));
    m_sequenceProtocol.insert("__concat__", StrPair("PyObject* " PYTHON_SELF_VAR ", PyObject* _other", "PyObject*"));

    // Sequence protocol structure members names
    m_sqFuncs["__concat__"] = "sq_concat";
    m_sqFuncs["__contains__"] = "sq_contains";
    m_sqFuncs["__getitem__"] = "sq_item";
    m_sqFuncs["__getslice__"] = "sq_slice";
    m_sqFuncs["__len__"] = "sq_length";
    m_sqFuncs["__setitem__"] = "sq_ass_item";
    m_sqFuncs["__setslice__"] = "sq_ass_slice";

    // mapping protocol function
    m_mappingProtocol.insert("__mlen__", StrPair("PyObject* " PYTHON_SELF_VAR, "Py_ssize_t"));
    m_mappingProtocol.insert("__mgetitem__", StrPair("PyObject* " PYTHON_SELF_VAR ", PyObject* _key", "PyObject*"));
    m_mappingProtocol.insert("__msetitem__", StrPair("PyObject* " PYTHON_SELF_VAR ", PyObject* _key, PyObject* _value", "int"));

    // Sequence protocol structure members names
    m_mpFuncs["__mlen__"] = "mp_length";
    m_mpFuncs["__mgetitem__"] = "mp_subscript";
    m_mpFuncs["__msetitem__"] = "mp_ass_subscript";
}

QString CppGenerator::fileNameForClass(const AbstractMetaClass *metaClass) const
{
    return metaClass->qualifiedCppName().toLower().replace("::", "_") + QLatin1String("_wrapper.cpp");
}

QList<AbstractMetaFunctionList> CppGenerator::filterGroupedOperatorFunctions(const AbstractMetaClass* metaClass,
                                                                             uint query)
{
    // ( func_name, num_args ) => func_list
    QMap<QPair<QString, int >, AbstractMetaFunctionList> results;
    foreach (AbstractMetaFunction* func, metaClass->operatorOverloads(query)) {
        if (func->isModifiedRemoved() || func->name() == "operator[]" || func->name() == "operator->")
            continue;
        int args;
        if (func->isComparisonOperator()) {
            args = -1;
        } else {
            args = func->arguments().size();
        }
        QPair<QString, int > op(func->name(), args);
        results[op].append(func);
    }
    return results.values();
}

static QString typeResolverString(const QString& type, QString typeName = QString(), bool isObject = false)
{
    if (typeName.isEmpty())
        typeName = QString("\"%1\"").arg(type);
    else if (!typeName.startsWith("typeid("))
        typeName = QString("\"%1\"").arg(typeName);
    return QString("Shiboken::TypeResolver::create%1TypeResolver< %2 >(%3)")
              .arg(isObject ? "Object" : "Value")
              .arg(type)
              .arg(typeName);
}

void CppGenerator::writeRegisterType(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString typeName = metaClass->qualifiedCppName();
    QString cppTypeName = QString("::%1").arg(typeName);
    QString reducedName = reduceTypeName(metaClass);

    if (!isObjectType(metaClass)) {
        s << INDENT << typeResolverString(cppTypeName, typeName) << ';' << endl;
        if (!reducedName.isEmpty())
            s << INDENT << typeResolverString(cppTypeName, reducedName) << ';' << endl;
    }

    s << INDENT << typeResolverString(cppTypeName, typeName+'*', true) << ';' << endl;
    if (!reducedName.isEmpty())
        s << INDENT << typeResolverString(cppTypeName, reducedName+'*', true) << ';' << endl;

    s << INDENT << typeResolverString(cppTypeName, QString("typeid(::%1).name()").arg(typeName), isObjectType(metaClass)) << ';' << endl;

    if (shouldGenerateCppWrapper(metaClass)) {
        s << INDENT;
        s << typeResolverString(cppTypeName, QString("typeid(::%1).name()").arg(wrapperName(metaClass)), isObjectType(metaClass));
        s << ';' << endl;
    }
}

void CppGenerator::writeRegisterType(QTextStream& s, const AbstractMetaEnum* metaEnum)
{
    QString fullName;
    QString shortName;
    if (metaEnum->enclosingClass()) {
        QString suffix = "::" + metaEnum->name();
        fullName = metaEnum->enclosingClass()->qualifiedCppName() + suffix;
        shortName = reduceTypeName(metaEnum->enclosingClass()) + metaEnum->name();
    } else  {
        fullName = metaEnum->name();
    }
}

void CppGenerator::writeToPythonFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    ErrorCode errorCode(0);
    s << "static PyObject* " << cpythonBaseName(metaClass) << "_ToPythonFunc(PyObject* " PYTHON_SELF_VAR ")" << endl;
    s << '{' << endl;
    writeCppSelfDefinition(s, metaClass);

    s << INDENT << "PyObject* " PYTHON_RETURN_VAR " = Shiboken::PythonConverter< ::" << metaClass->qualifiedCppName();
    s << " >::transformToPython(" CPP_SELF_VAR ");" << endl;

    writeFunctionReturnErrorCheckSection(s);
    s << INDENT << "return " PYTHON_RETURN_VAR ";" << endl;
    s << '}' << endl;
}

bool CppGenerator::hasBoolCast(const AbstractMetaClass* metaClass) const
{
    if (!useIsNullAsNbNonZero())
        return false;
    // TODO: This could be configurable someday
    const AbstractMetaFunction* func = metaClass->findFunction("isNull");
    if (!func || !func->type() || !func->type()->typeEntry()->isPrimitive() || !func->isPublic())
        return false;
    const PrimitiveTypeEntry* pte = static_cast<const PrimitiveTypeEntry*>(func->type()->typeEntry());
    while (pte->aliasedTypeEntry())
        pte = pte->aliasedTypeEntry();
    return func && func->isConstant() && pte->name() == "bool" && func->arguments().isEmpty();
}

/*!
    Function used to write the class generated binding code on the buffer
    \param s the output buffer
    \param metaClass the pointer to metaclass information
*/
void CppGenerator::generateClass(QTextStream &s, const AbstractMetaClass *metaClass)
{
    ReportHandler::debugSparse("Generating wrapper implementation for " + metaClass->fullName());

    // write license comment
    s << licenseComment() << endl;

    if (!avoidProtectedHack() && !metaClass->isNamespace() && !metaClass->hasPrivateDestructor()) {
        s << "//workaround to access protected functions" << endl;
        s << "#define protected public" << endl << endl;
    }

    // headers
    s << "// default includes" << endl;
    s << "#include <shiboken.h>" << endl;
    if (usePySideExtensions()) {
        s << "#include <pysidesignal.h>" << endl;
        s << "#include <pysideproperty.h>" << endl;
        s << "#include <pyside.h>" << endl;
        s << "#include <destroylistener.h>" << endl;
    }

    s << "#include <typeresolver.h>" << endl;
    s << "#include <typeinfo>" << endl;
    if (usePySideExtensions() && metaClass->isQObject()) {
        s << "#include <signalmanager.h>" << endl;
        s << "#include <pysidemetafunction.h>" << endl;
    }

    // The multiple inheritance initialization function
    // needs the 'set' class from C++ STL.
    if (hasMultipleInheritanceInAncestry(metaClass))
        s << "#include <set>" << endl;

    s << "#include \"" << getModuleHeaderFileName() << '"' << endl << endl;

    QString headerfile = fileNameForClass(metaClass);
    headerfile.replace(".cpp", ".h");
    s << "#include \"" << headerfile << '"' << endl;
    foreach (AbstractMetaClass* innerClass, metaClass->innerClasses()) {
        if (shouldGenerate(innerClass)) {
            QString headerfile = fileNameForClass(innerClass);
            headerfile.replace(".cpp", ".h");
            s << "#include \"" << headerfile << '"' << endl;
        }
    }

    AbstractMetaEnumList classEnums = metaClass->enums();
    foreach (AbstractMetaClass* innerClass, metaClass->innerClasses())
        lookForEnumsInClassesNotToBeGenerated(classEnums, innerClass);

    //Extra includes
    s << endl << "// Extra includes" << endl;
    QList<Include> includes = metaClass->typeEntry()->extraIncludes();
    foreach (AbstractMetaEnum* cppEnum, classEnums)
        includes.append(cppEnum->typeEntry()->extraIncludes());
    qSort(includes.begin(), includes.end());
    foreach (Include inc, includes)
        s << inc.toString() << endl;
    s << endl;

    if (metaClass->typeEntry()->typeFlags() & ComplexTypeEntry::Deprecated)
        s << "#Deprecated" << endl;

    //Use class base namespace
    const AbstractMetaClass *context = metaClass->enclosingClass();
    while(context) {
        if (context->isNamespace() && !context->enclosingClass()) {
            s << "using namespace " << context->qualifiedCppName() << ";" << endl;
            break;
        }
        context = context->enclosingClass();
    }

    s << endl;

    // class inject-code native/beginning
    if (!metaClass->typeEntry()->codeSnips().isEmpty()) {
        writeCodeSnips(s, metaClass->typeEntry()->codeSnips(), CodeSnip::Beginning, TypeSystem::NativeCode, metaClass);
        s << endl;
    }

    // python conversion rules
    if (metaClass->typeEntry()->hasTargetConversionRule()) {
        s << "// Python Conversion" << endl;
        s << metaClass->typeEntry()->conversionRule() << endl;
    }

    if (shouldGenerateCppWrapper(metaClass)) {
        s << "// Native ---------------------------------------------------------" << endl;
        s << endl;

        if (avoidProtectedHack() && usePySideExtensions()) {
            s << "void " << wrapperName(metaClass) << "::pysideInitQtMetaTypes()\n{\n";
            Indentation indent(INDENT);
            writeInitQtMetaTypeFunctionBody(s, metaClass);
            s << "}\n\n";
        }

        foreach (const AbstractMetaFunction* func, filterFunctions(metaClass)) {
            if ((func->isPrivate() && !visibilityModifiedToPrivate(func))
                || (func->isModifiedRemoved() && !func->isAbstract()))
                continue;
            if (func->isConstructor() && !func->isCopyConstructor() && !func->isUserAdded())
                writeConstructorNative(s, func);
            else if ((!avoidProtectedHack() || !metaClass->hasPrivateDestructor())
                     && (func->isVirtual() || func->isAbstract()))
                writeVirtualMethodNative(s, func);
        }

        if (!avoidProtectedHack() || !metaClass->hasPrivateDestructor()) {
            if (usePySideExtensions() && metaClass->isQObject())
                writeMetaObjectMethod(s, metaClass);
            writeDestructorNative(s, metaClass);
        }
    }

    Indentation indentation(INDENT);

    QString methodsDefinitions;
    QTextStream md(&methodsDefinitions);
    QString singleMethodDefinitions;
    QTextStream smd(&singleMethodDefinitions);

    s << endl << "// Target ---------------------------------------------------------" << endl << endl;
    s << "extern \"C\" {" << endl;
    foreach (AbstractMetaFunctionList allOverloads, getFunctionGroups(metaClass).values()) {
        AbstractMetaFunctionList overloads;
        foreach (AbstractMetaFunction* func, allOverloads) {
            if (!func->isAssignmentOperator()
                && !func->isCastOperator()
                && !func->isModifiedRemoved()
                && (!func->isPrivate() || func->functionType() == AbstractMetaFunction::EmptyFunction)
                && func->ownerClass() == func->implementingClass()
                && (func->name() != "qt_metacall"))
                overloads.append(func);
        }

        if (overloads.isEmpty())
            continue;

        const AbstractMetaFunction* rfunc = overloads.first();
        if (m_sequenceProtocol.contains(rfunc->name()) || m_mappingProtocol.contains(rfunc->name()))
            continue;

        if (rfunc->isConstructor())
            writeConstructorWrapper(s, overloads);
        // call operators
        else if (rfunc->name() == "operator()")
            writeMethodWrapper(s, overloads);
        else if (!rfunc->isOperatorOverload()) {
            writeMethodWrapper(s, overloads);
            if (OverloadData::hasStaticAndInstanceFunctions(overloads)) {
                QString methDefName = cpythonMethodDefinitionName(rfunc);
                smd << "static PyMethodDef " << methDefName << " = {" << endl;
                smd << INDENT;
                writeMethodDefinitionEntry(smd, overloads);
                smd << endl << "};" << endl << endl;
            }
            writeMethodDefinition(md, overloads);
        }
    }

    //ToPython used by Python Conversion
    if (metaClass->typeEntry()->hasTargetConversionRule()) {
        writeToPythonFunction(s, metaClass);
        md << INDENT << "{\"toPython\", (PyCFunction)" << cpythonBaseName(metaClass) << "_ToPythonFunc, METH_NOARGS}," << endl;
    }

    QString className = cpythonTypeName(metaClass).replace(QRegExp("_Type$"), "");

    if (metaClass->typeEntry()->isValue())
        writeCopyFunction(s, metaClass);

    // Write single method definitions
    s << singleMethodDefinitions;

    // Write methods definition
    s << "static PyMethodDef " << className << "_methods[] = {" << endl;
    s << methodsDefinitions << endl;
    if (metaClass->typeEntry()->isValue())
        s << INDENT << "{\"__copy__\", (PyCFunction)" << className << "___copy__" << ", METH_NOARGS}," << endl;
    s << INDENT << "{0} // Sentinel" << endl;
    s << "};" << endl << endl;

    // Write tp_getattro function
    if (usePySideExtensions() && metaClass->qualifiedCppName() == "QObject") {
        writeGetattroFunction(s, metaClass);
        s << endl;
        writeSetattroFunction(s, metaClass);
        s << endl;
    } else if (classNeedsGetattroFunction(metaClass)) {
        writeGetattroFunction(s, metaClass);
        s << endl;
    }

    if (hasBoolCast(metaClass)) {
        ErrorCode errorCode(-1);
        s << "static int " << cpythonBaseName(metaClass) << "___nb_bool(PyObject* " PYTHON_SELF_VAR ")" << endl;
        s << '{' << endl;
        writeCppSelfDefinition(s, metaClass);
        s << INDENT << "int result;" << endl;
        s << INDENT << BEGIN_ALLOW_THREADS << endl;
        s << INDENT << "result = !" CPP_SELF_VAR "->isNull();" << endl;
        s << INDENT << END_ALLOW_THREADS << endl;
        s << INDENT << "return result;" << endl;
        s << '}' << endl << endl;
    }

    if (supportsNumberProtocol(metaClass)) {
        QList<AbstractMetaFunctionList> opOverloads = filterGroupedOperatorFunctions(
                metaClass,
                AbstractMetaClass::ArithmeticOp
                | AbstractMetaClass::LogicalOp
                | AbstractMetaClass::BitwiseOp);

        foreach (AbstractMetaFunctionList allOverloads, opOverloads) {
            AbstractMetaFunctionList overloads;
            foreach (AbstractMetaFunction* func, allOverloads) {
                if (!func->isModifiedRemoved()
                    && !func->isPrivate()
                    && (func->ownerClass() == func->implementingClass() || func->isAbstract()))
                    overloads.append(func);
            }

            if (overloads.isEmpty())
                continue;

            writeMethodWrapper(s, overloads);
        }
    }

    if (supportsSequenceProtocol(metaClass)) {
        writeSequenceMethods(s, metaClass);
    }

    if (supportsMappingProtocol(metaClass)) {
        writeMappingMethods(s, metaClass);
    }

    if (metaClass->hasComparisonOperatorOverload()) {
        s << "// Rich comparison" << endl;
        writeRichCompareFunction(s, metaClass);
    }

    if (shouldGenerateGetSetList(metaClass)) {
        foreach (const AbstractMetaField* metaField, metaClass->fields()) {
            if (metaField->isStatic())
                continue;
            writeGetterFunction(s, metaField);
            if (!metaField->type()->isConstant())
                writeSetterFunction(s, metaField);
            s << endl;
        }

        s << "// Getters and Setters for " << metaClass->name() << endl;
        s << "static PyGetSetDef " << cpythonGettersSettersDefinitionName(metaClass) << "[] = {" << endl;
        foreach (const AbstractMetaField* metaField, metaClass->fields()) {
            if (metaField->isStatic())
                continue;

            bool hasSetter = !metaField->type()->isConstant();
            s << INDENT << "{const_cast<char*>(\"" << metaField->name() << "\"), ";
            s << cpythonGetterFunctionName(metaField);
            s << ", " << (hasSetter ? cpythonSetterFunctionName(metaField) : "0");
            s << "}," << endl;
        }
        s << INDENT << "{0}  // Sentinel" << endl;
        s << "};" << endl << endl;
    }

    s << "} // extern \"C\"" << endl << endl;

    if (!metaClass->typeEntry()->hashFunction().isEmpty())
        writeHashFunction(s, metaClass);

    // Write tp_traverse and tp_clear functions.
    writeTpTraverseFunction(s, metaClass);
    writeTpClearFunction(s, metaClass);

    writeClassDefinition(s, metaClass);
    s << endl;

    if (metaClass->isPolymorphic() && metaClass->baseClass())
        writeTypeDiscoveryFunction(s, metaClass);


    foreach (AbstractMetaEnum* cppEnum, classEnums) {
        if (cppEnum->isAnonymous() || cppEnum->isPrivate())
            continue;

        bool hasFlags = cppEnum->typeEntry()->flags();
        if (hasFlags) {
            writeFlagsMethods(s, cppEnum);
            writeFlagsNumberMethodsDefinition(s, cppEnum);
            s << endl;
        }

        if (hasFlags) {
            // Write Enum as Flags definition (at the moment used only by QFlags<enum>)
            writeFlagsDefinition(s, cppEnum);
            s << endl;
        }
    }
    s << endl;

    writeClassRegister(s, metaClass);

    // class inject-code native/end
    if (!metaClass->typeEntry()->codeSnips().isEmpty()) {
        writeCodeSnips(s, metaClass->typeEntry()->codeSnips(), CodeSnip::End, TypeSystem::NativeCode, metaClass);
        s << endl;
    }
}

void CppGenerator::writeConstructorNative(QTextStream& s, const AbstractMetaFunction* func)
{
    Indentation indentation(INDENT);
    s << functionSignature(func, wrapperName(func->ownerClass()) + "::", "",
                           OriginalTypeDescription | SkipDefaultValues);
    s << " : ";
    writeFunctionCall(s, func);
    s << " {" << endl;
    const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
    writeCodeSnips(s, func->injectedCodeSnips(), CodeSnip::Beginning, TypeSystem::NativeCode, func, lastArg);
    s << INDENT << "// ... middle" << endl;
    writeCodeSnips(s, func->injectedCodeSnips(), CodeSnip::End, TypeSystem::NativeCode, func, lastArg);
    s << '}' << endl << endl;
}

void CppGenerator::writeDestructorNative(QTextStream &s, const AbstractMetaClass *metaClass)
{
    Indentation indentation(INDENT);
    s << wrapperName(metaClass) << "::~" << wrapperName(metaClass) << "()" << endl << '{' << endl;
    // kill pyobject
    s << INDENT << "SbkObject* wrapper = Shiboken::BindingManager::instance().retrieveWrapper(this);" << endl;
    s << INDENT << "Shiboken::Object::destroy(wrapper, this);" << endl;
    s << '}' << endl;
}

static bool allArgumentsRemoved(const AbstractMetaFunction* func)
{
    if (func->arguments().isEmpty())
        return false;
    foreach (const AbstractMetaArgument* arg, func->arguments()) {
        if (!func->argumentRemoved(arg->argumentIndex() + 1))
            return false;
    }
    return true;
}

QString CppGenerator::getVirtualFunctionReturnTypeName(const AbstractMetaFunction* func)
{
    if (!func->type())
        return "\"\"";

    if (!func->typeReplaced(0).isEmpty())
        return '"' + func->typeReplaced(0) + '"';

    // SbkType would return null when the type is a container.
    if (func->type()->typeEntry()->isContainer())
        return '"' + reinterpret_cast<const ContainerTypeEntry*>(func->type()->typeEntry())->typeName() + '"';

    if (avoidProtectedHack()) {
        const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(func->type());
        if (metaEnum && metaEnum->isProtected())
            return '"' + protectedEnumSurrogateName(metaEnum) + '"';
    }

    if (func->type()->isPrimitive())
        return '"' + func->type()->name() + '"';

    return QString("Shiboken::SbkType< %1 >()->tp_name").arg(func->type()->typeEntry()->qualifiedCppName());
}

void CppGenerator::writeVirtualMethodNative(QTextStream&s, const AbstractMetaFunction* func)
{
    //skip metaObject function, this will be written manually ahead
    if (usePySideExtensions() && func->ownerClass() && func->ownerClass()->isQObject() &&
        ((func->name() == "metaObject") || (func->name() == "qt_metacall")))
        return;

    const TypeEntry* type = func->type() ? func->type()->typeEntry() : 0;
    const QString funcName = func->isOperatorOverload() ? pythonOperatorFunctionName(func) : func->name();

    QString prefix = QString("%1::").arg(wrapperName(func->ownerClass()));
    s << functionSignature(func, prefix, "", Generator::SkipDefaultValues|Generator::OriginalTypeDescription) << endl;
    s << '{' << endl;

    Indentation indentation(INDENT);

    QString defaultReturnExpr;
    if (func->type()) {
        foreach (FunctionModification mod, func->modifications()) {
            foreach (ArgumentModification argMod, mod.argument_mods) {
                if (argMod.index == 0 && !argMod.replacedDefaultExpression.isEmpty()) {
                    QRegExp regex("%(\\d+)");
                    defaultReturnExpr = argMod.replacedDefaultExpression;
                    int offset = 0;
                    while ((offset = regex.indexIn(defaultReturnExpr, offset)) != -1) {
                        int argId = regex.cap(1).toInt() - 1;
                        if (argId < 0 || argId > func->arguments().count()) {
                            ReportHandler::warning("The expression used in return value contains an invalid index.");
                            break;
                        }
                        defaultReturnExpr.replace(regex.cap(0), func->arguments()[argId]->name());
                    }
                }
            }
        }
        if (defaultReturnExpr.isEmpty())
            defaultReturnExpr = minimalConstructor(func->type());
        if (defaultReturnExpr.isEmpty()) {
            QString errorMsg = QString(MIN_CTOR_ERROR_MSG).arg(func->type()->cppSignature());
            ReportHandler::warning(errorMsg);
            s << endl << INDENT << "#error " << errorMsg << endl;
        }
    }

    if (func->isAbstract() && func->isModifiedRemoved()) {
        ReportHandler::warning(QString("Pure virtual method '%1::%2' must be implement but was "\
                                       "completely removed on type system.")
                                  .arg(func->ownerClass()->name())
                                  .arg(func->minimalSignature()));
        s << INDENT << "return " << defaultReturnExpr << ';' << endl;
        s << '}' << endl << endl;
        return;
    }

    //Write declaration/native injected code
    if (func->hasInjectedCode()) {
        CodeSnipList snips = func->injectedCodeSnips();
        const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
        writeCodeSnips(s, snips, CodeSnip::Declaration, TypeSystem::NativeCode, func, lastArg);
        s << endl;
    }

    s << INDENT << "Shiboken::GilState gil;" << endl;

    // Get out of virtual method call if someone already threw an error.
    s << INDENT << "if (PyErr_Occurred())" << endl;
    {
        Indentation indentation(INDENT);
        s << INDENT << "return " << defaultReturnExpr << ';' << endl;
    }

    s << INDENT << "Shiboken::AutoDecRef " PYTHON_OVERRIDE_VAR "(Shiboken::BindingManager::instance().getOverride(this, \"";
    s << funcName << "\"));" << endl;

    s << INDENT << "if (" PYTHON_OVERRIDE_VAR ".isNull()) {" << endl;
    {
        Indentation indentation(INDENT);
        CodeSnipList snips;
        if (func->hasInjectedCode()) {
            snips = func->injectedCodeSnips();
            const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
            writeCodeSnips(s, snips, CodeSnip::Beginning, TypeSystem::ShellCode, func, lastArg);
            s << endl;
        }

        if (func->isAbstract()) {
            s << INDENT << "PyErr_SetString(PyExc_NotImplementedError, \"pure virtual method '";
            s << func->ownerClass()->name() << '.' << funcName;
            s << "()' not implemented.\");" << endl;
            s << INDENT << "return " << (func->type() ? defaultReturnExpr : "");
        } else {
            s << INDENT << "gil.release();" << endl;
            s << INDENT << "return this->::" << func->implementingClass()->qualifiedCppName() << "::";
            writeFunctionCall(s, func, Generator::VirtualCall);
        }
    }
    s << ';' << endl;
    s << INDENT << '}' << endl << endl;

    writeConversionRule(s, func, TypeSystem::TargetLangCode);

    s << INDENT << "Shiboken::AutoDecRef " PYTHON_ARGS "(";

    if (func->arguments().isEmpty() || allArgumentsRemoved(func)) {
        s << "PyTuple_New(0));" << endl;
    } else {
        QStringList argConversions;
        foreach (const AbstractMetaArgument* arg, func->arguments()) {
            if (func->argumentRemoved(arg->argumentIndex() + 1))
                continue;

            QString argConv;
            QTextStream ac(&argConv);
            const PrimitiveTypeEntry* argType = (const PrimitiveTypeEntry*) arg->type()->typeEntry();
            bool convert = argType->isObject()
                            || arg->type()->isQObject()
                            || argType->isValue()
                            || arg->type()->isValuePointer()
                            || arg->type()->isNativePointer()
                            || argType->isFlags()
                            || argType->isEnum()
                            || argType->isContainer()
                            || arg->type()->isReference();

            if (!convert && argType->isPrimitive()) {
                if (argType->basicAliasedTypeEntry())
                    argType = argType->basicAliasedTypeEntry();
                convert = !m_formatUnits.contains(argType->name());
            }

            Indentation indentation(INDENT);
            ac << INDENT;
            if (!func->conversionRule(TypeSystem::TargetLangCode, arg->argumentIndex() + 1).isEmpty()) {
                // Has conversion rule.
                ac << QString("%1"CONV_RULE_OUT_VAR_SUFFIX).arg(arg->name());
            } else {
                QString argName = arg->name();
                if (avoidProtectedHack()) {
                    const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(arg->type());
                    if (metaEnum && metaEnum->isProtected()) {
                        argName.prepend(protectedEnumSurrogateName(metaEnum) + '(');
                        argName.append(')');
                    }
                }
                if (convert)
                    writeToPythonConversion(ac, arg->type(), func->ownerClass(), argName);
                else
                    ac << argName;
            }

            argConversions << argConv;
        }

        s << "Py_BuildValue(\"(" << getFormatUnitString(func, false) << ")\"," << endl;
        s << argConversions.join(",\n") << endl;
        s << INDENT << "));" << endl;
    }

    bool invalidateReturn = false;
    foreach (FunctionModification funcMod, func->modifications()) {
        foreach (ArgumentModification argMod, funcMod.argument_mods) {
            if (argMod.resetAfterUse) {
                s << INDENT << "bool invalidateArg" << argMod.index;
                s << " = PyTuple_GET_ITEM(" PYTHON_ARGS ", " << argMod.index - 1 << ")->ob_refcnt == 1;" << endl;
            } else if (argMod.index == 0 && argMod.ownerships[TypeSystem::TargetLangCode] == TypeSystem::CppOwnership) {
                invalidateReturn = true;
            }
        }
    }
    s << endl;

    CodeSnipList snips;
    if (func->hasInjectedCode()) {
        snips = func->injectedCodeSnips();

        if (injectedCodeUsesPySelf(func))
            s << INDENT << "PyObject* pySelf = BindingManager::instance().retrieveWrapper(this);" << endl;

        const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
        writeCodeSnips(s, snips, CodeSnip::Beginning, TypeSystem::NativeCode, func, lastArg);
        s << endl;
    }

    if (!injectedCodeCallsPythonOverride(func)) {
        s << INDENT;
        s << "Shiboken::AutoDecRef " PYTHON_RETURN_VAR "(PyObject_Call(" PYTHON_OVERRIDE_VAR ", " PYTHON_ARGS ", NULL));" << endl;

        s << INDENT << "// An error happened in python code!" << endl;
        s << INDENT << "if (" PYTHON_RETURN_VAR ".isNull()) {" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "PyErr_Print();" << endl;
            s << INDENT << "return " << defaultReturnExpr << ';' << endl;
        }
        s << INDENT << '}' << endl;

        if (type) {
            if (invalidateReturn)
                s << INDENT << "bool invalidateArg0 = " PYTHON_RETURN_VAR "->ob_refcnt == 1;" << endl;

            if (func->type() && func->typeReplaced(0) != "PyObject") {
                s << INDENT << "// Check return type" << endl;
                s << INDENT << "bool typeIsValid = ";
                writeTypeCheck(s, func->type(), PYTHON_RETURN_VAR, isNumber(func->type()->typeEntry()), func->typeReplaced(0));
                s << ';' << endl;

                s << INDENT << "if (!typeIsValid";
                s << (isPointerToWrapperType(func->type()) ? " && " PYTHON_RETURN_VAR " != Py_None" : "");
                s << ") {" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "Shiboken::warning(PyExc_RuntimeWarning, 2, "\
                                   "\"Invalid return value in function %s, expected %s, got %s.\", \"";
                    s << func->ownerClass()->name() << '.' << funcName << "\", " << getVirtualFunctionReturnTypeName(func);
                    s << ", " PYTHON_RETURN_VAR "->ob_type->tp_name);" << endl;
                    s << INDENT << "return " << defaultReturnExpr << ';' << endl;
                }
                s << INDENT << '}' << endl;
            }

            if (!func->conversionRule(TypeSystem::NativeCode, 0).isEmpty()) {
                // Has conversion rule.
                writeConversionRule(s, func, TypeSystem::NativeCode, CPP_RETURN_VAR);
            } else if (!injectedCodeHasReturnValueAttribution(func, TypeSystem::NativeCode)) {
                writePythonToCppTypeConversion(s, func->type(), PYTHON_RETURN_VAR, CPP_RETURN_VAR, func->implementingClass());
            }
        }
    }

    if (invalidateReturn) {
        s << INDENT << "if (invalidateArg0)" << endl;
        Indentation indentation(INDENT);
        s << INDENT << "Shiboken::Object::releaseOwnership(" << PYTHON_RETURN_VAR  ".object());" << endl;
    }

    foreach (FunctionModification funcMod, func->modifications()) {
        foreach (ArgumentModification argMod, funcMod.argument_mods) {
            if (argMod.resetAfterUse) {
                s << INDENT << "if (invalidateArg" << argMod.index << ')' << endl;
                Indentation indentation(INDENT);
                s << INDENT << "Shiboken::Object::invalidate(PyTuple_GET_ITEM(" PYTHON_ARGS ", ";
                s << (argMod.index - 1) << "));" << endl;
            } else if (argMod.ownerships.contains(TypeSystem::NativeCode)
                       && argMod.index == 0 && argMod.ownerships[TypeSystem::NativeCode] == TypeSystem::CppOwnership) {
                s << INDENT << "if (Shiboken::Object::checkType(" PYTHON_RETURN_VAR "))" << endl;
                Indentation indent(INDENT);
                s << INDENT << "Shiboken::Object::releaseOwnership(" PYTHON_RETURN_VAR ");" << endl;
            }
        }
    }

    if (func->hasInjectedCode()) {
        s << endl;
        const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
        writeCodeSnips(s, snips, CodeSnip::End, TypeSystem::NativeCode, func, lastArg);
    }

    if (type) {
        s << INDENT << "return ";
        // TODO-IMPROVEMENTS: try to put this on writePythonToCppTypeConversion.
        if (avoidProtectedHack() && type->isEnum()) {
            const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(type);
            bool isProtectedEnum = metaEnum && metaEnum->isProtected();
            if (isProtectedEnum) {
                QString typeCast;
                if (metaEnum->enclosingClass())
                    typeCast += QString("::%1").arg(metaEnum->enclosingClass()->qualifiedCppName());
                typeCast += QString("::%1").arg(metaEnum->name());
                s << '(' << typeCast << ')';
            }
        }
        s << CPP_RETURN_VAR ";" << endl;
    }

    s << '}' << endl << endl;
}

void CppGenerator::writeMetaObjectMethod(QTextStream& s, const AbstractMetaClass* metaClass)
{
    Indentation indentation(INDENT);
    QString wrapperClassName = wrapperName(metaClass);
    s << "const QMetaObject* " << wrapperClassName << "::metaObject() const" << endl;
    s << '{' << endl;
    s << INDENT << "#if QT_VERSION >= 0x040700" << endl;
    s << INDENT << "if (QObject::d_ptr->metaObject) return QObject::d_ptr->metaObject;" << endl;
    s << INDENT << "#endif" << endl;
    s << INDENT << "SbkObject* pySelf = Shiboken::BindingManager::instance().retrieveWrapper(this);" << endl;
    s << INDENT << "return PySide::SignalManager::retriveMetaObject(reinterpret_cast<PyObject*>(pySelf));" << endl;
    s << '}' << endl << endl;

    // qt_metacall function
    s << "int " << wrapperClassName << "::qt_metacall(QMetaObject::Call call, int id, void** args)" << endl;
    s << "{" << endl;

    AbstractMetaFunction *func = NULL;
    AbstractMetaFunctionList list = metaClass->queryFunctionsByName("qt_metacall");
    if (list.size() == 1)
        func = list[0];

    CodeSnipList snips;
    if (func) {
        snips = func->injectedCodeSnips();
        if (func->isUserAdded()) {
            CodeSnipList snips = func->injectedCodeSnips();
            writeCodeSnips(s, snips, CodeSnip::Any, TypeSystem::NativeCode, func);
        }
    }

    s << INDENT << "int result = " << metaClass->qualifiedCppName() << "::qt_metacall(call, id, args);" << endl;
    s << INDENT << "return result < 0 ? result : PySide::SignalManager::qt_metacall(this, call, id, args);" << endl;
    s << "}" << endl << endl;

    // qt_metacast function
    writeMetaCast(s, metaClass);
}

void CppGenerator::writeMetaCast(QTextStream& s, const AbstractMetaClass* metaClass)
{
    Indentation indentation(INDENT);
    QString wrapperClassName = wrapperName(metaClass);
    s << "void* " << wrapperClassName << "::qt_metacast(const char* _clname)" << endl;
    s << '{' << endl;
    s << INDENT << "if (!_clname) return 0;" << endl;
    s << INDENT << "SbkObject* pySelf = Shiboken::BindingManager::instance().retrieveWrapper(this);" << endl;
    s << INDENT << "if (pySelf && PySide::inherits(Py_TYPE(pySelf), _clname))" << endl;
    s << INDENT << INDENT << "return static_cast<void*>(const_cast< " << wrapperClassName << "* >(this));" << endl;
    s << INDENT << "return " << metaClass->qualifiedCppName() << "::qt_metacast(_clname);" << endl;
    s << "}" << endl << endl;
}

void CppGenerator::writeMethodWrapperPreamble(QTextStream& s, OverloadData& overloadData)
{
    const AbstractMetaFunction* rfunc = overloadData.referenceFunction();
    const AbstractMetaClass* ownerClass = rfunc->ownerClass();
    int minArgs = overloadData.minArgs();
    int maxArgs = overloadData.maxArgs();
    bool initPythonArguments;
    bool usesNamedArguments;

    // If method is a constructor...
    if (rfunc->isConstructor()) {
        // Check if the right constructor was called.
        if (!ownerClass->hasPrivateDestructor()) {
            s << INDENT << "if (Shiboken::Object::isUserType(" PYTHON_SELF_VAR ") && !Shiboken::ObjectType::canCallConstructor(" PYTHON_SELF_VAR "->ob_type, Shiboken::SbkType< ::";
            s << ownerClass->qualifiedCppName() << " >()))" << endl;
            Indentation indent(INDENT);
            s << INDENT << "return " << m_currentErrorCode << ';' << endl << endl;
        }
        // Declare pointer for the underlying C++ object.
        s << INDENT << "::";
        s << (shouldGenerateCppWrapper(ownerClass) ? wrapperName(ownerClass) : ownerClass->qualifiedCppName());
        s << "* cptr = 0;" << endl;

        initPythonArguments = maxArgs > 0;
        usesNamedArguments = !ownerClass->isQObject() && overloadData.hasArgumentWithDefaultValue();

    } else {
        if (rfunc->implementingClass() &&
            (!rfunc->implementingClass()->isNamespace() && overloadData.hasInstanceFunction())) {
            writeCppSelfDefinition(s, rfunc, overloadData.hasStaticFunction());
        }
        if (!rfunc->isInplaceOperator() && overloadData.hasNonVoidReturnType())
            s << INDENT << "PyObject* " PYTHON_RETURN_VAR " = 0;" << endl;

        initPythonArguments = minArgs != maxArgs || maxArgs > 1;
        usesNamedArguments = rfunc->isCallOperator() || overloadData.hasArgumentWithDefaultValue();
    }

    if (maxArgs > 0)
        s << INDENT << "int overloadId = -1;" << endl;

    if (usesNamedArguments && !rfunc->isCallOperator())
        s << INDENT << "int numNamedArgs = (kwds ? PyDict_Size(kwds) : 0);" << endl;

    if (initPythonArguments) {
        s << INDENT << "int numArgs = ";
        if (minArgs == 0 && maxArgs == 1 && !rfunc->isConstructor() && !pythonFunctionWrapperUsesListOfArguments(overloadData))
            s << "(" PYTHON_ARG " == 0 ? 0 : 1);" << endl;
        else
            writeArgumentsInitializer(s, overloadData);
    }
}

void CppGenerator::writeConstructorWrapper(QTextStream& s, const AbstractMetaFunctionList overloads)
{
    ErrorCode errorCode(-1);
    OverloadData overloadData(overloads, this);

    const AbstractMetaFunction* rfunc = overloadData.referenceFunction();
    const AbstractMetaClass* metaClass = rfunc->ownerClass();

    s << "static int" << endl;
    s << cpythonFunctionName(rfunc) << "(PyObject* " PYTHON_SELF_VAR ", PyObject* args, PyObject* kwds)" << endl;
    s << '{' << endl;

    QSet<QString> argNamesSet;
    if (usePySideExtensions() && metaClass->isQObject()) {
        // Write argNames variable with all known argument names.
        foreach (const AbstractMetaFunction* func, overloadData.overloads()) {
            foreach (const AbstractMetaArgument* arg, func->arguments()) {
                if (arg->defaultValueExpression().isEmpty() || func->argumentRemoved(arg->argumentIndex() + 1))
                    continue;
                argNamesSet << arg->name();
            }
        }
        QStringList argNamesList = argNamesSet.toList();
        qSort(argNamesList.begin(), argNamesList.end());
        if (argNamesList.isEmpty())
            s << INDENT << "const char** argNames = 0;" << endl;
        else
            s << INDENT << "const char* argNames[] = {\"" << argNamesList.join("\", \"") << "\"};" << endl;
        s << INDENT << "const QMetaObject* metaObject;" << endl;
    }

    s << INDENT << "SbkObject* sbkSelf = reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR ");" << endl;

    if (metaClass->isAbstract() || metaClass->baseClassNames().size() > 1) {
        s << INDENT << "SbkObjectType* type = reinterpret_cast<SbkObjectType*>(" PYTHON_SELF_VAR "->ob_type);" << endl;
        s << INDENT << "SbkObjectType* myType = reinterpret_cast<SbkObjectType*>(" << cpythonTypeNameExt(metaClass->typeEntry()) << ");" << endl;
    }

    if (metaClass->isAbstract()) {
        s << INDENT << "if (type == myType) {" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "PyErr_SetString(PyExc_NotImplementedError," << endl;
            {
                Indentation indentation(INDENT);
                s << INDENT << "\"'" << metaClass->qualifiedCppName();
            }
            s << "' represents a C++ abstract class and cannot be instantiated\");" << endl;
            s << INDENT << "return " << m_currentErrorCode << ';' << endl;
        }
        s << INDENT << '}' << endl << endl;
    }

    if (metaClass->baseClassNames().size() > 1) {
        if (!metaClass->isAbstract()) {
            s << INDENT << "if (type != myType) {" << endl;
        }
        {
            Indentation indentation(INDENT);
            s << INDENT << "Shiboken::ObjectType::copyMultimpleheritance(type, myType);" << endl;
        }
        if (!metaClass->isAbstract())
            s << INDENT << '}' << endl << endl;
    }

    writeMethodWrapperPreamble(s, overloadData);

    s << endl;

    bool hasPythonConvertion = metaClass->typeEntry()->hasTargetConversionRule();
    if (hasPythonConvertion) {
        s << INDENT << "// Try python conversion rules" << endl;
        s << INDENT << "cptr = Shiboken::PythonConverter< ::" << metaClass->qualifiedCppName() << " >::transformFromPython(" PYTHON_ARGS "[0]);" << endl;
        s << INDENT << "if (!cptr) {" << endl;
    }

    if (overloadData.maxArgs() > 0)
        writeOverloadedFunctionDecisor(s, overloadData);

    writeFunctionCalls(s, overloadData);
    s << endl;

    if (hasPythonConvertion)
        s << INDENT << "}" << endl;

    s << INDENT << "if (PyErr_Occurred() || !Shiboken::Object::setCppPointer(sbkSelf, Shiboken::SbkType< ::" << metaClass->qualifiedCppName() << " >(), cptr)) {" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "delete cptr;" << endl;
        s << INDENT << "return " << m_currentErrorCode << ';' << endl;
    }
    s << INDENT << '}' << endl;
    if (overloadData.maxArgs() > 0) {
        s << INDENT << "if (!cptr) goto " << cpythonFunctionName(rfunc) << "_TypeError;" << endl;
        s << endl;
    }

    s << INDENT << "Shiboken::Object::setValidCpp(sbkSelf, true);" << endl;
    // If the created C++ object has a C++ wrapper the ownership is assigned to Python
    // (first "1") and the flag indicating that the Python wrapper holds an C++ wrapper
    // is marked as true (the second "1"). Otherwise the default values apply:
    // Python owns it and C++ wrapper is false.
    if (shouldGenerateCppWrapper(overloads.first()->ownerClass()))
        s << INDENT << "Shiboken::Object::setHasCppWrapper(sbkSelf, true);" << endl;
    s << INDENT << "Shiboken::BindingManager::instance().registerWrapper(sbkSelf, cptr);" << endl;

    // Create metaObject and register signal/slot
    if (metaClass->isQObject() && usePySideExtensions()) {
        s << endl << INDENT << "// QObject setup" << endl;
        s << INDENT << "PySide::Signal::updateSourceObject(" PYTHON_SELF_VAR ");" << endl;
        s << INDENT << "metaObject = cptr->metaObject(); // <- init python qt properties" << endl;
        s << INDENT << "if (kwds && !PySide::fillQtProperties(" PYTHON_SELF_VAR ", metaObject, kwds, argNames, " << argNamesSet.count() << "))" << endl;
        {
            Indentation indentation(INDENT);
            s << INDENT << "return " << m_currentErrorCode << ';' << endl;
        }
    }

    // Constructor code injections, position=end
    bool hasCodeInjectionsAtEnd = false;
    foreach(AbstractMetaFunction* func, overloads) {
        foreach (CodeSnip cs, func->injectedCodeSnips()) {
            if (cs.position == CodeSnip::End) {
                hasCodeInjectionsAtEnd = true;
                break;
            }
        }
    }
    if (hasCodeInjectionsAtEnd) {
        // FIXME: C++ arguments are not available in code injection on constructor when position = end.
        s << INDENT << "switch(overloadId) {" << endl;
        foreach(AbstractMetaFunction* func, overloads) {
            Indentation indent(INDENT);
            foreach (CodeSnip cs, func->injectedCodeSnips()) {
                if (cs.position == CodeSnip::End) {
                    s << INDENT << "case " << metaClass->functions().indexOf(func) << ':' << endl;
                    s << INDENT << '{' << endl;
                    {
                        Indentation indent(INDENT);
                        writeCodeSnips(s, func->injectedCodeSnips(), CodeSnip::End, TypeSystem::TargetLangCode, func);
                    }
                    s << INDENT << '}' << endl;
                    break;
                }
            }
        }
        s << '}' << endl;
    }

    s << endl;
    s << endl << INDENT << "return 1;" << endl;
    if (overloadData.maxArgs() > 0)
        writeErrorSection(s, overloadData);
    s << '}' << endl << endl;
}

void CppGenerator::writeMethodWrapper(QTextStream& s, const AbstractMetaFunctionList overloads)
{
    OverloadData overloadData(overloads, this);
    const AbstractMetaFunction* rfunc = overloadData.referenceFunction();

    int maxArgs = overloadData.maxArgs();

    s << "static PyObject* ";
    s << cpythonFunctionName(rfunc) << "(PyObject* " PYTHON_SELF_VAR;
    if (maxArgs > 0) {
        s << ", PyObject* " << (pythonFunctionWrapperUsesListOfArguments(overloadData) ? "args" : PYTHON_ARG);
        if (overloadData.hasArgumentWithDefaultValue() || rfunc->isCallOperator())
            s << ", PyObject* kwds";
    }
    s << ')' << endl << '{' << endl;

    writeMethodWrapperPreamble(s, overloadData);

    s << endl;

    /*
     * Make sure reverse <</>> operators defined in other classes (specially from other modules)
     * are called. A proper and generic solution would require an reengineering in the operator
     * system like the extended converters.
     *
     * Solves #119 - QDataStream <</>> operators not working for QPixmap
     * http://bugs.openbossa.org/show_bug.cgi?id=119
     */
    bool hasReturnValue = overloadData.hasNonVoidReturnType();
    bool callExtendedReverseOperator = hasReturnValue
                                       && !rfunc->isInplaceOperator()
                                       && !rfunc->isCallOperator()
                                       && rfunc->isOperatorOverload();
    if (callExtendedReverseOperator) {
        QString revOpName = ShibokenGenerator::pythonOperatorFunctionName(rfunc).insert(2, 'r');
        if (rfunc->isBinaryOperator()) {
            s << INDENT << "if (!isReverse" << endl;
            {
                Indentation indent(INDENT);
                s << INDENT << "&& Shiboken::Object::checkType(" PYTHON_ARG ")" << endl;
                s << INDENT << "&& !PyObject_TypeCheck(" PYTHON_ARG ", " PYTHON_SELF_VAR "->ob_type)" << endl;
                s << INDENT << "&& PyObject_HasAttrString(" PYTHON_ARG ", const_cast<char*>(\"" << revOpName << "\"))) {" << endl;

                // This PyObject_CallMethod call will emit lots of warnings like
                // "deprecated conversion from string constant to char *" during compilation
                // due to the method name argument being declared as "char*" instead of "const char*"
                // issue 6952 http://bugs.python.org/issue6952
                s << INDENT << "PyObject* revOpMethod = PyObject_GetAttrString(" PYTHON_ARG ", const_cast<char*>(\"" << revOpName << "\"));" << endl;
                s << INDENT << "if (revOpMethod && PyCallable_Check(revOpMethod)) {" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << PYTHON_RETURN_VAR " = PyObject_CallFunction(revOpMethod, const_cast<char*>(\"O\"), " PYTHON_SELF_VAR ");" << endl;
                    s << INDENT << "if (PyErr_Occurred() && (PyErr_ExceptionMatches(PyExc_NotImplementedError)";
                    s << " || PyErr_ExceptionMatches(PyExc_AttributeError))) {" << endl;
                    {
                        Indentation indent(INDENT);
                        s << INDENT << "PyErr_Clear();" << endl;
                        s << INDENT << "Py_XDECREF(" PYTHON_RETURN_VAR ");" << endl;
                        s << INDENT << PYTHON_RETURN_VAR " = 0;" << endl;
                    }
                    s << INDENT << '}' << endl;
                }
                s << INDENT << "}" << endl;
                s << INDENT << "Py_XDECREF(revOpMethod);" << endl << endl;
            }
            s << INDENT << "}" << endl;
        }
        s << INDENT << "// Do not enter here if other object has implemented a reverse operator." << endl;
        s << INDENT << "if (!" PYTHON_RETURN_VAR ") {" << endl << endl;
    }

    if (maxArgs > 0)
        writeOverloadedFunctionDecisor(s, overloadData);

    writeFunctionCalls(s, overloadData);

    if (callExtendedReverseOperator)
        s << endl << INDENT << "} // End of \"if (!" PYTHON_RETURN_VAR ")\"" << endl;

    s << endl;

    writeFunctionReturnErrorCheckSection(s, hasReturnValue && !rfunc->isInplaceOperator());

    if (hasReturnValue) {
        if (rfunc->isInplaceOperator()) {
            s << INDENT << "Py_INCREF(" PYTHON_SELF_VAR ");\n";
            s << INDENT << "return " PYTHON_SELF_VAR ";\n";
        } else {
            s << INDENT << "return " PYTHON_RETURN_VAR ";\n";
        }
    } else {
        s << INDENT << "Py_RETURN_NONE;" << endl;
    }

    if (maxArgs > 0)
        writeErrorSection(s, overloadData);

    s << '}' << endl << endl;
}

void CppGenerator::writeArgumentsInitializer(QTextStream& s, OverloadData& overloadData)
{
    const AbstractMetaFunction* rfunc = overloadData.referenceFunction();
    s << "PyTuple_GET_SIZE(args);" << endl;

    int minArgs = overloadData.minArgs();
    int maxArgs = overloadData.maxArgs();

    s << INDENT << "PyObject* ";
    s << PYTHON_ARGS "[] = {" << QString(maxArgs, '0').split("", QString::SkipEmptyParts).join(", ") << "};" << endl;
    s << endl;

    if (overloadData.hasVarargs()) {
        maxArgs--;
        if (minArgs > maxArgs)
            minArgs = maxArgs;

        s << INDENT << "PyObject* nonvarargs = PyTuple_GetSlice(args, 0, " << maxArgs << ");" << endl;
        s << INDENT << "Shiboken::AutoDecRef auto_nonvarargs(nonvarargs);" << endl;
        s << INDENT << PYTHON_ARGS "[" << maxArgs << "] = PyTuple_GetSlice(args, " << maxArgs << ", numArgs);" << endl;
        s << INDENT << "Shiboken::AutoDecRef auto_varargs(" PYTHON_ARGS "[" << maxArgs << "]);" << endl;
        s << endl;
    }

    bool usesNamedArguments = overloadData.hasArgumentWithDefaultValue();

    s << INDENT << "// invalid argument lengths" << endl;
    bool ownerClassIsQObject = rfunc->ownerClass() && rfunc->ownerClass()->isQObject() && rfunc->isConstructor();
    if (usesNamedArguments) {
        if (!ownerClassIsQObject) {
            s << INDENT << "if (numArgs" << (overloadData.hasArgumentWithDefaultValue() ? " + numNamedArgs" : "") << " > " << maxArgs << ") {" << endl;
            {
                Indentation indent(INDENT);
                s << INDENT << "PyErr_SetString(PyExc_TypeError, \"" << fullPythonFunctionName(rfunc) << "(): too many arguments\");" << endl;
                s << INDENT << "return " << m_currentErrorCode << ';' << endl;
            }
            s << INDENT << '}';
        }
        if (minArgs > 0) {
            if (ownerClassIsQObject)
                s << INDENT;
            else
                s << " else ";
            s << "if (numArgs < " << minArgs << ") {" << endl;
            {
                Indentation indent(INDENT);
                s << INDENT << "PyErr_SetString(PyExc_TypeError, \"" << fullPythonFunctionName(rfunc) << "(): not enough arguments\");" << endl;
                s << INDENT << "return " << m_currentErrorCode << ';' << endl;
            }
            s << INDENT << '}';
        }
    }
    QList<int> invalidArgsLength = overloadData.invalidArgumentLengths();
    if (!invalidArgsLength.isEmpty()) {
        QStringList invArgsLen;
        foreach (int i, invalidArgsLength)
            invArgsLen << QString("numArgs == %1").arg(i);
        if (usesNamedArguments && (!ownerClassIsQObject || minArgs > 0))
            s << " else ";
        else
            s << INDENT;
        s << "if (" << invArgsLen.join(" || ") << ")" << endl;
        Indentation indent(INDENT);
        s << INDENT << "goto " << cpythonFunctionName(rfunc) << "_TypeError;";
    }
    s << endl << endl;

    QString funcName;
    if (rfunc->isOperatorOverload())
        funcName = ShibokenGenerator::pythonOperatorFunctionName(rfunc);
    else
        funcName = rfunc->name();

    QString argsVar = overloadData.hasVarargs() ?  "nonvarargs" : "args";
    s << INDENT << "if (!";
    if (usesNamedArguments)
        s << "PyArg_ParseTuple(" << argsVar << ", \"|" << QByteArray(maxArgs, 'O') << ':' << funcName << '"';
    else
        s << "PyArg_UnpackTuple(" << argsVar << ", \"" << funcName << "\", " << minArgs << ", " << maxArgs;
    QStringList palist;
    for (int i = 0; i < maxArgs; i++)
        palist << QString("&(" PYTHON_ARGS "[%1])").arg(i);
    s << ", " << palist.join(", ") << "))" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "return " << m_currentErrorCode << ';' << endl;
    }
    s << endl;
}

void CppGenerator::writeCppSelfDefinition(QTextStream& s, const AbstractMetaClass* metaClass, bool hasStaticOverload, bool cppSelfAsReference)
{
    bool useWrapperClass = avoidProtectedHack() && metaClass->hasProtectedMembers();
    QString className = useWrapperClass ? wrapperName(metaClass) : QString("::%1").arg(metaClass->qualifiedCppName());

    QString cppSelfAttribution;
    if (cppSelfAsReference) {
        QString cast = useWrapperClass ? QString("(%1*)").arg(className) : QString();
        cppSelfAttribution = QString("%1& %2 = *(%3%4)")
                                .arg(className)
                                .arg(CPP_SELF_VAR)
                                .arg(cast)
                                .arg(cpythonWrapperCPtr(metaClass, PYTHON_SELF_VAR));
    } else {
        s << INDENT << className << "* " CPP_SELF_VAR " = 0;" << endl;
        cppSelfAttribution = QString("%1 = %2%3")
                                .arg(CPP_SELF_VAR)
                                .arg(useWrapperClass ? QString("(%1*)").arg(className) : "")
                                .arg(cpythonWrapperCPtr(metaClass, PYTHON_SELF_VAR));
    }

    // Checks if the underlying C++ object is valid.
    if (hasStaticOverload && !cppSelfAsReference) {
        s << INDENT << "if (" PYTHON_SELF_VAR ") {" << endl;
        {
            Indentation indent(INDENT);
            writeInvalidPyObjectCheck(s, PYTHON_SELF_VAR);
            s << INDENT << cppSelfAttribution << ';' << endl;
        }
        s << INDENT << '}' << endl;
        return;
    }

    writeInvalidPyObjectCheck(s, PYTHON_SELF_VAR);
    s << INDENT << cppSelfAttribution << ';' << endl;
}

void CppGenerator::writeCppSelfDefinition(QTextStream& s, const AbstractMetaFunction* func, bool hasStaticOverload)
{
    if (!func->ownerClass() || func->isConstructor())
        return;

    if (func->isOperatorOverload() && func->isBinaryOperator()) {
        QString checkFunc = cpythonCheckFunction(func->ownerClass()->typeEntry());
        s << INDENT << "bool isReverse = " << checkFunc << "(" PYTHON_ARG ")" << endl;
        {
            Indentation indent1(INDENT);
            Indentation indent2(INDENT);
            Indentation indent3(INDENT);
            Indentation indent4(INDENT);
            s << INDENT << "&& !" << checkFunc << "(" PYTHON_SELF_VAR ");" << endl;
        }
        s << INDENT << "if (isReverse)" << endl;
        Indentation indent(INDENT);
        s << INDENT << "std::swap(" PYTHON_SELF_VAR ", " PYTHON_ARG ");" << endl;
    }

    writeCppSelfDefinition(s, func->ownerClass(), hasStaticOverload);
}

void CppGenerator::writeErrorSection(QTextStream& s, OverloadData& overloadData)
{
    const AbstractMetaFunction* rfunc = overloadData.referenceFunction();
    s << endl << INDENT << cpythonFunctionName(rfunc) << "_TypeError:" << endl;
    Indentation indentation(INDENT);
    QString funcName = fullPythonFunctionName(rfunc);

    QString argsVar = pythonFunctionWrapperUsesListOfArguments(overloadData) ? "args" : PYTHON_ARG;
    if (verboseErrorMessagesDisabled()) {
        s << INDENT << "Shiboken::setErrorAboutWrongArguments(" << argsVar << ", \"" << funcName << "\", 0);" << endl;
    } else {
        QStringList overloadSignatures;
        foreach (const AbstractMetaFunction* f, overloadData.overloads()) {
            QStringList args;
            foreach(AbstractMetaArgument* arg, f->arguments()) {
                QString strArg;
                AbstractMetaType* argType = arg->type();
                if (isCString(argType)) {
                    strArg = "\"SBK_STR_NAME\"";
                } else if (argType->isPrimitive()) {
                    const PrimitiveTypeEntry* ptp = reinterpret_cast<const PrimitiveTypeEntry*>(argType->typeEntry());
                    while (ptp->aliasedTypeEntry())
                        ptp = ptp->aliasedTypeEntry();
                    strArg = ptp->name();
                    if (strArg == "QString") {
                        strArg = "unicode";
                    } else if (strArg == "QChar") {
                        strArg = "1-unicode";
                    } else {
                        strArg = ptp->name().replace(QRegExp("^signed\\s+"), "");
                        if (strArg == "double")
                            strArg = "float";
                    }
                } else if (argType->typeEntry()->isContainer()) {
                    strArg = argType->fullName();
                    if (strArg == "QList" || strArg == "QVector"
                        || strArg == "QLinkedList" || strArg == "QStack"
                        || strArg == "QQueue") {
                        strArg = "list";
                    } else if (strArg == "QMap" || strArg == "QHash"
                               || strArg == "QMultiMap" || strArg == "QMultiHash") {
                        strArg = "dict";
                    } else if (strArg == "QPair") {
                        strArg == "2-tuple";
                    }
                } else {
                    strArg = argType->fullName();
                    if (strArg == "PyUnicode")
                        strArg = "unicode";
                    else if (strArg == "PyString")
                        strArg = "str";
                    else if (strArg == "PyBytes")
                        strArg = "\"SBK_STR_NAME\"";
                    else if (strArg == "PySequece")
                        strArg = "list";
                    else if (strArg == "PyTuple")
                        strArg = "tuple";
                    else if (strArg == "PyDict")
                        strArg = "dict";
                    else if (strArg == "PyObject")
                        strArg = "object";
                    else if (strArg == "PyCallable")
                        strArg = "callable";
                    else if (strArg == "uchar")
                        strArg = "buffer"; // This depends on an inject code to be true, but if it's not true
                                           // the function wont work at all, so it must be true.
                }
                if (!arg->defaultValueExpression().isEmpty()) {
                    strArg += " = ";
                    if ((isCString(argType) || isPointerToWrapperType(argType))
                        && arg->defaultValueExpression() == "0") {
                        strArg += "None";
                    } else {
                        strArg += arg->defaultValueExpression().replace("::", ".").replace("\"", "\\\"");
                    }
                }
                args << strArg;
            }
            overloadSignatures << "\""+args.join(", ")+"\"";
        }
        s << INDENT << "const char* overloads[] = {" << overloadSignatures.join(", ") << ", 0};" << endl;
        s << INDENT << "Shiboken::setErrorAboutWrongArguments(" << argsVar << ", \"" << funcName << "\", overloads);" << endl;
    }
    s << INDENT << "return " << m_currentErrorCode << ';' << endl;
}

void CppGenerator::writeFunctionReturnErrorCheckSection(QTextStream& s, bool hasReturnValue)
{
    s << INDENT << "if (PyErr_Occurred()" << (hasReturnValue ? " || !" PYTHON_RETURN_VAR : "") << ") {" << endl;
    {
        Indentation indent(INDENT);
        if (hasReturnValue)
            s << INDENT << "Py_XDECREF(" PYTHON_RETURN_VAR ");" << endl;
        s << INDENT << "return " << m_currentErrorCode << ';' << endl;
    }
    s << INDENT << '}' << endl;
}

void CppGenerator::writeInvalidPyObjectCheck(QTextStream& s, const QString& pyObj)
{
    s << INDENT << "if (!Shiboken::Object::isValid(" << pyObj << "))" << endl;
    Indentation indent(INDENT);
    s << INDENT << "return " << m_currentErrorCode << ';' << endl;
}

void CppGenerator::writeTypeCheck(QTextStream& s, const AbstractMetaType* argType, QString argumentName, bool isNumber, QString customType, bool rejectNull)
{
    QString customCheck;
    if (!customType.isEmpty()) {
        AbstractMetaType* metaType;
        customCheck = guessCPythonCheckFunction(customType, &metaType);
        if (metaType)
            argType = metaType;
    }

    QString typeCheck;
    if (customCheck.isEmpty())
        typeCheck = cpythonIsConvertibleFunction(argType, argType->isEnum() ? false : isNumber);
    else
        typeCheck = customCheck;
    typeCheck.append(QString("(%1)").arg(argumentName));

    if (rejectNull)
        typeCheck = QString("(%1 != Py_None && %2)").arg(argumentName).arg(typeCheck);

    s << typeCheck;
}

void CppGenerator::writeTypeCheck(QTextStream& s, const OverloadData* overloadData, QString argumentName)
{
    QSet<const TypeEntry*> numericTypes;

    foreach (OverloadData* od, overloadData->previousOverloadData()->nextOverloadData()) {
        foreach (const AbstractMetaFunction* func, od->overloads()) {
            const AbstractMetaArgument* arg = od->argument(func);

            if (!arg->type()->isPrimitive())
                continue;
            if (ShibokenGenerator::isNumber(arg->type()->typeEntry()))
                numericTypes << arg->type()->typeEntry();
        }
    }

    // This condition trusts that the OverloadData object will arrange for
    // PyInt type to come after the more precise numeric types (e.g. float and bool)
    const AbstractMetaType* argType = overloadData->argType();
    bool numberType = numericTypes.count() == 1 || ShibokenGenerator::isPyInt(argType);
    QString customType = (overloadData->hasArgumentTypeReplace() ? overloadData->argumentTypeReplaced() : "");
    bool rejectNull = shouldRejectNullPointerArgument(overloadData->referenceFunction(), overloadData->argPos());
    writeTypeCheck(s, argType, argumentName, numberType, customType, rejectNull);
}

void CppGenerator::writeArgumentConversion(QTextStream& s,
                                           const AbstractMetaType* argType,
                                           const QString& argName, const QString& pyArgName,
                                           const AbstractMetaClass* context,
                                           const QString& defaultValue,
                                           bool castArgumentAsUnused)
{
    if (argType->typeEntry()->isCustom() || argType->typeEntry()->isVarargs())
        return;
    if (isWrapperType(argType))
        writeInvalidPyObjectCheck(s, pyArgName);
    writePythonToCppTypeConversion(s, argType, pyArgName, argName, context, defaultValue);
    if (castArgumentAsUnused)
        writeUnusedVariableCast(s, argName);
}

const AbstractMetaType* CppGenerator::getArgumentType(const AbstractMetaFunction* func, int argPos)
{
    if (argPos < 0 || argPos > func->arguments().size()) {
        ReportHandler::warning(QString("Argument index for function '%1' out of range.").arg(func->signature()));
        return 0;
    }

    const AbstractMetaType* argType = 0;
    QString typeReplaced = func->typeReplaced(argPos);
    if (typeReplaced.isEmpty())
        argType = (argPos == 0) ? func->type() : func->arguments().at(argPos-1)->type();
    else
        argType = buildAbstractMetaTypeFromString(typeReplaced);
    if (!argType && !m_knownPythonTypes.contains(typeReplaced)) {
        ReportHandler::warning(QString("Unknown type '%1' used as argument type replacement "\
                                       "in function '%2', the generated code may be broken.")
                                      .arg(typeReplaced)
                                      .arg(func->signature()));
    }
    return argType;
}

void CppGenerator::writePythonToCppTypeConversion(QTextStream& s,
                                                  const AbstractMetaType* type,
                                                  const QString& pyIn,
                                                  const QString& cppOut,
                                                  const AbstractMetaClass* context,
                                                  const QString& defaultValue)
{
    if (type->typeEntry()->isCustom() || type->typeEntry()->isVarargs())
        return;

    QString conversion;
    QTextStream c(&conversion);
    writeToCppConversion(c, type, context, pyIn);

    QString typeName;
    QString cppOutAux = QString("%1_tmp").arg(cppOut);

    // Value type that has default value.
    if (type->isValue() && !defaultValue.isEmpty())
        s << INDENT << type->typeEntry()->name() << ' ' << cppOutAux << " = " << defaultValue << ';' << endl;

    if (typeName.isEmpty()) {
        // exclude const on Objects
        Options flags = getConverterOptions(type);
        typeName = translateTypeForWrapperMethod(type, context, flags).trimmed();
    }

    if (!defaultValue.isEmpty()) {
        conversion.prepend(QString("%1 ? ").arg(pyIn));
        conversion.append(QString(" : %1").arg(type->isValue() ? cppOutAux : defaultValue));
    }

    s << INDENT << typeName << " " << cppOut << " = " << conversion << ';' << endl;
}

static void addConversionRuleCodeSnippet(CodeSnipList& snippetList, QString& rule,
                                         TypeSystem::Language conversionLanguage,
                                         TypeSystem::Language snippetLanguage,
                                         QString outputName = QString(),
                                         QString inputName = QString())
{
    if (rule.isEmpty())
        return;
    if (snippetLanguage == TypeSystem::TargetLangCode) {
        rule.replace("%in", inputName);
        rule.replace("%out", QString("%1_out").arg(outputName));
    } else {
        rule.replace("%out", outputName);
    }
    CodeSnip snip(0, snippetLanguage);
    snip.position = (snippetLanguage == TypeSystem::NativeCode) ? CodeSnip::Any : CodeSnip::Beginning;
    snip.addCode(rule);
    snippetList << snip;
}

void CppGenerator::writeConversionRule(QTextStream& s, const AbstractMetaFunction* func, TypeSystem::Language language)
{
    CodeSnipList snippets;
    foreach (AbstractMetaArgument* arg, func->arguments()) {
        QString rule = func->conversionRule(language, arg->argumentIndex() + 1);
        addConversionRuleCodeSnippet(snippets, rule, language, TypeSystem::TargetLangCode,
                                     arg->name(), arg->name());
    }
    writeCodeSnips(s, snippets, CodeSnip::Beginning, TypeSystem::TargetLangCode, func);
}

void CppGenerator::writeConversionRule(QTextStream& s, const AbstractMetaFunction* func, TypeSystem::Language language, const QString& outputVar)
{
    CodeSnipList snippets;
    QString rule = func->conversionRule(language, 0);
    addConversionRuleCodeSnippet(snippets, rule, language, language, outputVar);
    writeCodeSnips(s, snippets, CodeSnip::Any, language, func);
}

void CppGenerator::writeNoneReturn(QTextStream& s, const AbstractMetaFunction* func, bool thereIsReturnValue)
{
    if (thereIsReturnValue && (!func->type() || func->argumentRemoved(0)) && !injectedCodeHasReturnValueAttribution(func)) {
        s << INDENT << PYTHON_RETURN_VAR " = Py_None;" << endl;
        s << INDENT << "Py_INCREF(Py_None);" << endl;
    }
}

void CppGenerator::writeOverloadedFunctionDecisor(QTextStream& s, const OverloadData& overloadData)
{
    s << INDENT << "// Overloaded function decisor" << endl;
    const AbstractMetaFunction* rfunc = overloadData.referenceFunction();
    QList<const AbstractMetaFunction*> functionOverloads = overloadData.overloadsWithoutRepetition();
    for (int i = 0; i < functionOverloads.count(); i++)
        s << INDENT << "// " << i << ": " << functionOverloads.at(i)->minimalSignature() << endl;
    writeOverloadedFunctionDecisorEngine(s, &overloadData);
    s << endl;

    // Ensure that the direct overload that called this reverse
    // is called.
    if (rfunc->isOperatorOverload() && !rfunc->isCallOperator()) {
        s << INDENT << "if (isReverse && overloadId == -1) {" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "PyErr_SetString(PyExc_NotImplementedError, \"reverse operator not implemented.\");" << endl;
            s << INDENT << "return 0;" << endl;
        }
        s << INDENT << "}" << endl << endl;
    }

    s << INDENT << "// Function signature not found." << endl;
    s << INDENT << "if (overloadId == -1) goto " << cpythonFunctionName(overloadData.referenceFunction()) << "_TypeError;" << endl;
    s << endl;
}

void CppGenerator::writeOverloadedFunctionDecisorEngine(QTextStream& s, const OverloadData* parentOverloadData)
{
    bool hasDefaultCall = parentOverloadData->nextArgumentHasDefaultValue();
    const AbstractMetaFunction* referenceFunction = parentOverloadData->referenceFunction();

    // If the next argument has not an argument with a default value, it is still possible
    // that one of the overloads for the current overload data has its final occurrence here.
    // If found, the final occurrence of a method is attributed to the referenceFunction
    // variable to be used further on this method on the conditional that identifies default
    // method calls.
    if (!hasDefaultCall) {
        foreach (const AbstractMetaFunction* func, parentOverloadData->overloads()) {
            if (parentOverloadData->isFinalOccurrence(func)) {
                referenceFunction = func;
                hasDefaultCall = true;
                break;
            }
        }
    }

    int maxArgs = parentOverloadData->maxArgs();
    // Python constructors always receive multiple arguments.
    bool usePyArgs = pythonFunctionWrapperUsesListOfArguments(*parentOverloadData);

    // Functions without arguments are identified right away.
    if (maxArgs == 0) {
        s << INDENT << "overloadId = " << parentOverloadData->headOverloadData()->overloads().indexOf(referenceFunction);
        s << "; // " << referenceFunction->minimalSignature() << endl;
        return;

    // To decide if a method call is possible at this point the current overload
    // data object cannot be the head, since it is just an entry point, or a root,
    // for the tree of arguments and it does not represent a valid method call.
    } else if (!parentOverloadData->isHeadOverloadData()) {
        bool isLastArgument = parentOverloadData->nextOverloadData().isEmpty();
        bool signatureFound = parentOverloadData->overloads().size() == 1;

        // The current overload data describes the last argument of a signature,
        // so the method can be identified right now.
        if (isLastArgument || (signatureFound && !hasDefaultCall)) {
            const AbstractMetaFunction* func = parentOverloadData->referenceFunction();
            s << INDENT << "overloadId = " << parentOverloadData->headOverloadData()->overloads().indexOf(func);
            s << "; // " << func->minimalSignature() << endl;
            return;
        }
    }

    bool isFirst = true;

    // If the next argument has a default value the decisor can perform a method call;
    // it just need to check if the number of arguments received from Python are equal
    // to the number of parameters preceding the argument with the default value.
    if (hasDefaultCall) {
        isFirst = false;
        int numArgs = parentOverloadData->argPos() + 1;
        s << INDENT << "if (numArgs == " << numArgs << ") {" << endl;
        {
            Indentation indent(INDENT);
            const AbstractMetaFunction* func = referenceFunction;
            foreach (OverloadData* overloadData, parentOverloadData->nextOverloadData()) {
                const AbstractMetaFunction* defValFunc = overloadData->getFunctionWithDefaultValue();
                if (defValFunc) {
                    func = defValFunc;
                    break;
                }
            }
            s << INDENT << "overloadId = " << parentOverloadData->headOverloadData()->overloads().indexOf(func);
            s << "; // " << func->minimalSignature() << endl;
        }
        s << INDENT << '}';
    }

    foreach (OverloadData* overloadData, parentOverloadData->nextOverloadData()) {
        bool signatureFound = overloadData->overloads().size() == 1
                                && !overloadData->getFunctionWithDefaultValue()
                                && !overloadData->findNextArgWithDefault();

        const AbstractMetaFunction* refFunc = overloadData->referenceFunction();

        QStringList typeChecks;
        QString pyArgName = (usePyArgs && maxArgs > 1) ? QString(PYTHON_ARGS "[%1]").arg(overloadData->argPos()) : PYTHON_ARG;
        OverloadData* od = overloadData;
        int startArg = od->argPos();
        int sequenceArgCount = 0;
        while (od && !od->argType()->isVarargs()) {
            bool typeReplacedByPyObject = od->argumentTypeReplaced() == "PyObject";
            if (!typeReplacedByPyObject) {
                if (usePyArgs)
                    pyArgName = QString(PYTHON_ARGS "[%1]").arg(od->argPos());
                QString typeCheck;
                QTextStream tck(&typeCheck);
                writeTypeCheck(tck, od, pyArgName);
                typeChecks << typeCheck;
            }

            sequenceArgCount++;

            if (od->nextOverloadData().isEmpty()
                || od->nextArgumentHasDefaultValue()
                || od->nextOverloadData().size() != 1
                || od->overloads().size() != od->nextOverloadData().first()->overloads().size()) {
                overloadData = od;
                od = 0;
            } else {
                od = od->nextOverloadData().first();
            }
        }

        if (usePyArgs && signatureFound) {
            AbstractMetaArgumentList args = refFunc->arguments();
            int lastArgIsVarargs = (int) (args.size() > 1 && args.last()->type()->isVarargs());
            int numArgs = args.size() - OverloadData::numberOfRemovedArguments(refFunc) - lastArgIsVarargs;
            typeChecks.prepend(QString("numArgs %1 %2").arg(lastArgIsVarargs ? ">=" : "==").arg(numArgs));
        } else if (sequenceArgCount > 1) {
            typeChecks.prepend(QString("numArgs >= %1").arg(startArg + sequenceArgCount));
        } else if (refFunc->isOperatorOverload() && !refFunc->isCallOperator()) {
            typeChecks.prepend(QString("%1isReverse").arg(refFunc->isReverseOperator() ? "" : "!"));
        }

        if (isFirst) {
            isFirst = false;
            s << INDENT;
        } else {
            s << " else ";
        }
        s << "if (";
        if (typeChecks.isEmpty()) {
            s << "true";
        } else {
            Indentation indent(INDENT);
            QString separator;
            QTextStream sep(&separator);
            sep << endl << INDENT << "&& ";
            s << typeChecks.join(separator);
        }
        s << ") {" << endl;
        {
            Indentation indent(INDENT);
            writeOverloadedFunctionDecisorEngine(s, overloadData);
        }
        s << INDENT << "}";
    }
    s << endl;
}

void CppGenerator::writeFunctionCalls(QTextStream& s, const OverloadData& overloadData)
{
    QList<const AbstractMetaFunction*> overloads = overloadData.overloadsWithoutRepetition();
    s << INDENT << "// Call function/method" << endl;
    s << INDENT << (overloads.count() > 1 ? "switch (overloadId) " : "") << '{' << endl;
    {
        Indentation indent(INDENT);
        if (overloads.count() == 1) {
            writeSingleFunctionCall(s, overloadData, overloads.first());
        } else {
            for (int i = 0; i < overloads.count(); i++) {
                const AbstractMetaFunction* func = overloads.at(i);
                s << INDENT << "case " << i << ": // " << func->minimalSignature() << endl;
                s << INDENT << '{' << endl;
                {
                    Indentation indent(INDENT);
                    writeSingleFunctionCall(s, overloadData, func);
                    s << INDENT << "break;" << endl;
                }
                s << INDENT << '}' << endl;
            }
        }
    }
    s << INDENT << '}' << endl;
}

void CppGenerator::writeSingleFunctionCall(QTextStream& s, const OverloadData& overloadData, const AbstractMetaFunction* func)
{
    if (func->isDeprecated()) {
        s << INDENT << "Shiboken::warning(PyExc_DeprecationWarning, 1, \"Function: '"
                    << func->signature().replace("::", ".")
                    << "' is marked as deprecated, please check the documentation for more information.\");" << endl;
    }

    if (func->functionType() == AbstractMetaFunction::EmptyFunction) {
        s << INDENT << "PyErr_Format(PyExc_TypeError, \"%s is a private method.\", \"" << func->signature().replace("::", ".") << "\");" << endl;
        s << INDENT << "return " << m_currentErrorCode << ';' << endl;
        return;
    }

    const AbstractMetaClass* implementingClass = overloadData.referenceFunction()->implementingClass();
    bool usePyArgs = pythonFunctionWrapperUsesListOfArguments(overloadData);

    // Handle named arguments.
    writeNamedArgumentResolution(s, func, usePyArgs);

    bool injectCodeCallsFunc = injectedCodeCallsCppFunction(func);
    bool mayHaveUnunsedArguments = !func->isUserAdded() && func->hasInjectedCode() && injectCodeCallsFunc;
    int removedArgs = 0;
    for (int argIdx = 0; argIdx < func->arguments().count(); ++argIdx) {
        bool hasConversionRule = !func->conversionRule(TypeSystem::NativeCode, argIdx + 1).isEmpty();
        const AbstractMetaArgument* arg = func->arguments().at(argIdx);
        if (func->argumentRemoved(argIdx + 1)) {
            if (!arg->defaultValueExpression().isEmpty()) {
                QString cppArgRemoved = QString(CPP_ARG_REMOVED"%1").arg(argIdx);
                s << INDENT << getFullTypeName(arg->type()) << ' ' << cppArgRemoved;
                s << " = " << guessScopeForDefaultValue(func, arg) << ';' << endl;
                writeUnusedVariableCast(s, cppArgRemoved);
            } else if (!injectCodeCallsFunc && !func->isUserAdded() && !hasConversionRule) {
                // When an argument is removed from a method signature and no other means of calling
                // the method are provided (as with code injection) the generator must abort.
                qFatal(qPrintable(QString("No way to call '%1::%2' with the modifications described in the type system.")
                                     .arg(func->ownerClass()->name())
                                     .arg(func->signature())), NULL);
            }
            removedArgs++;
            continue;
        }
        if (hasConversionRule)
            continue;
        const AbstractMetaType* argType = getArgumentType(func, argIdx + 1);
        if (!argType || (mayHaveUnunsedArguments && !injectedCodeUsesArgument(func, argIdx)))
            continue;
        int argPos = argIdx - removedArgs;
        QString argName = QString(CPP_ARG"%1").arg(argPos);
        QString pyArgName = usePyArgs ? QString(PYTHON_ARGS "[%1]").arg(argPos) : PYTHON_ARG;
        QString defaultValue = guessScopeForDefaultValue(func, arg);
        writeArgumentConversion(s, argType, argName, pyArgName, implementingClass, defaultValue, func->isUserAdded());
    }

    s << endl;

    int numRemovedArgs = OverloadData::numberOfRemovedArguments(func);

    s << INDENT << "if(!PyErr_Occurred()) {" << endl;
    {
        Indentation indentation(INDENT);
        writeMethodCall(s, func, func->arguments().size() - numRemovedArgs);
        if (!func->isConstructor())
            writeNoneReturn(s, func, overloadData.hasNonVoidReturnType());
    }
    s << INDENT << "}"  << endl;
}

void CppGenerator::writeNamedArgumentResolution(QTextStream& s, const AbstractMetaFunction* func, bool usePyArgs)
{
    AbstractMetaArgumentList args = OverloadData::getArgumentsWithDefaultValues(func);
    if (!args.isEmpty()) {
        s << INDENT << "if (kwds) {" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "const char* errorArgName = 0;" << endl;
            s << INDENT << "PyObject* ";
            foreach (const AbstractMetaArgument* arg, args) {
                int pyArgIndex = arg->argumentIndex() - OverloadData::numberOfRemovedArguments(func, arg->argumentIndex());
                QString pyArgName = usePyArgs ? QString(PYTHON_ARGS "[%1]").arg(pyArgIndex) : PYTHON_ARG;
                s << "value = PyDict_GetItemString(kwds, \"" << arg->name() << "\");" << endl;
                s << INDENT << "if (value) {" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "if (" << pyArgName << ")" << endl;
                    {
                        Indentation indent(INDENT);
                        s << INDENT << "errorArgName = \"" << arg->name() << "\";" << endl;
                    }
                    s << INDENT << "else if (";
                    writeTypeCheck(s, arg->type(), "value", isNumber(arg->type()->typeEntry()));
                    s << ')' << endl;
                    {
                        Indentation indent(INDENT);
                        s << INDENT << pyArgName << " = value;" << endl;
                    }
                    s << INDENT << "else" << endl;
                    {
                        Indentation indent(INDENT);
                        s << INDENT << "goto " << cpythonFunctionName(func) << "_TypeError;" << endl;
                    }
                }
                s << INDENT << '}' << endl;
                s << INDENT;
            }
            s << "if (errorArgName) {" << endl;
            {
                Indentation indent(INDENT);
                s << INDENT << "PyErr_Format(PyExc_TypeError, \"" << fullPythonFunctionName(func);
                s << "(): got multiple values for keyword argument '%s'\", errorArgName);" << endl;
                s << INDENT << "return " << m_currentErrorCode << ';' << endl;
            }
            s << INDENT << '}' << endl;

        }
        s << INDENT << '}' << endl;
    }
}

QString CppGenerator::argumentNameFromIndex(const AbstractMetaFunction* func, int argIndex, const AbstractMetaClass** wrappedClass)
{
    *wrappedClass = 0;
    QString pyArgName;
    if (argIndex == -1) {
        pyArgName = QString(PYTHON_SELF_VAR);
        *wrappedClass = func->implementingClass();
    } else if (argIndex == 0) {
        AbstractMetaType* returnType = getTypeWithoutContainer(func->type());
        if (returnType) {
            pyArgName = PYTHON_RETURN_VAR;
            *wrappedClass = classes().findClass(returnType->typeEntry()->name());
        } else {
            ReportHandler::warning("Invalid Argument index on function modification: " + func->name());
        }
    } else {
        int realIndex = argIndex - 1 - OverloadData::numberOfRemovedArguments(func, argIndex - 1);
        AbstractMetaType* argType = getTypeWithoutContainer(func->arguments().at(realIndex)->type());

        if (argType) {
            *wrappedClass = classes().findClass(argType->typeEntry()->name());
            if (argIndex == 1
                && !func->isConstructor()
                && OverloadData::isSingleArgument(getFunctionGroups(func->implementingClass())[func->name()]))
                pyArgName = QString(PYTHON_ARG);
            else
                pyArgName = QString(PYTHON_ARGS "[%1]").arg(argIndex - 1);
        }
    }
    return pyArgName;
}

void CppGenerator::writeMethodCall(QTextStream& s, const AbstractMetaFunction* func, int maxArgs)
{
    s << INDENT << "// " << func->minimalSignature() << (func->isReverseOperator() ? " [reverse operator]": "") << endl;
    if (func->isConstructor()) {
        foreach (CodeSnip cs, func->injectedCodeSnips()) {
            if (cs.position == CodeSnip::End) {
                s << INDENT << "overloadId = " << func->ownerClass()->functions().indexOf(const_cast<AbstractMetaFunction* const>(func)) << ';' << endl;
                break;
            }
        }
    }

    if (func->isAbstract()) {
        s << INDENT << "if (Shiboken::Object::hasCppWrapper(reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR "))) {\n";
        {
            Indentation indent(INDENT);
            s << INDENT << "PyErr_SetString(PyExc_NotImplementedError, \"pure virtual method '";
            s << func->ownerClass()->name() << '.' << func->name() << "()' not implemented.\");" << endl;
            s << INDENT << "return " << m_currentErrorCode << ';' << endl;
        }
        s << INDENT << "}\n";
    }

    // Used to provide contextual information to custom code writer function.
    const AbstractMetaArgument* lastArg = 0;

    CodeSnipList snips;
    if (func->hasInjectedCode()) {
        snips = func->injectedCodeSnips();

        // Find the last argument available in the method call to provide
        // the injected code writer with information to avoid invalid replacements
        // on the %# variable.
        if (maxArgs > 0 && maxArgs < func->arguments().size() - OverloadData::numberOfRemovedArguments(func)) {
            int removedArgs = 0;
            for (int i = 0; i < maxArgs + removedArgs; i++) {
                lastArg = func->arguments().at(i);
                if (func->argumentRemoved(i + 1))
                    removedArgs++;
            }
        } else if (maxArgs != 0 && !func->arguments().isEmpty()) {
            lastArg = func->arguments().last();
        }

        writeCodeSnips(s, snips, CodeSnip::Beginning, TypeSystem::TargetLangCode, func, lastArg);
        s << endl;
    }

    writeConversionRule(s, func, TypeSystem::NativeCode);

    if (!func->isUserAdded()) {
        QStringList userArgs;
        if (!func->isCopyConstructor()) {
            int removedArgs = 0;
            for (int i = 0; i < maxArgs + removedArgs; i++) {
                const AbstractMetaArgument* arg = func->arguments().at(i);
                bool hasConversionRule = !func->conversionRule(TypeSystem::NativeCode, arg->argumentIndex() + 1).isEmpty();
                if (func->argumentRemoved(i + 1)) {
                    // If some argument with default value is removed from a
                    // method signature, the said value must be explicitly
                    // added to the method call.
                    removedArgs++;

                    // If have conversion rules I will use this for removed args
                    if (hasConversionRule)
                        userArgs << QString("%1"CONV_RULE_OUT_VAR_SUFFIX).arg(arg->name());
                    else if (!arg->defaultValueExpression().isEmpty())
                        userArgs << QString(CPP_ARG_REMOVED"%1").arg(i);
                } else {
                    int idx = arg->argumentIndex() - removedArgs;
                    QString argName = hasConversionRule
                                      ? QString("%1"CONV_RULE_OUT_VAR_SUFFIX).arg(arg->name())
                                      : QString(CPP_ARG"%1").arg(idx);
                    userArgs << argName;
                }
            }

            // If any argument's default value was modified the method must be called
            // with this new value whenever the user doesn't pass an explicit value to it.
            // Also, any unmodified default value coming after the last user specified
            // argument and before the modified argument must be explicitly stated.
            QStringList otherArgs;
            bool otherArgsModified = false;
            bool argsClear = true;
            for (int i = func->arguments().size() - 1; i >= maxArgs + removedArgs; i--) {
                const AbstractMetaArgument* arg = func->arguments().at(i);
                bool defValModified = arg->defaultValueExpression() != arg->originalDefaultValueExpression();
                bool hasConversionRule = !func->conversionRule(TypeSystem::NativeCode, arg->argumentIndex() + 1).isEmpty();
                if (argsClear && !defValModified && !hasConversionRule)
                    continue;
                else
                    argsClear = false;
                otherArgsModified |= defValModified || hasConversionRule || func->argumentRemoved(i + 1);
                if (hasConversionRule)
                    otherArgs.prepend(QString("%1"CONV_RULE_OUT_VAR_SUFFIX).arg(arg->name()));
                else
                    otherArgs.prepend(QString(CPP_ARG_REMOVED"%1").arg(i));
            }
            if (otherArgsModified)
                userArgs << otherArgs;
        }

        bool isCtor = false;
        QString methodCall;
        QTextStream mc(&methodCall);

        if (func->isOperatorOverload() && !func->isCallOperator()) {
            QByteArray firstArg("(*" CPP_SELF_VAR ")");
            if (func->isPointerOperator())
                firstArg.remove(1, 1); // remove the de-reference operator

            QByteArray secondArg(CPP_ARG0);
            if (!func->isUnaryOperator() && shouldDereferenceArgumentPointer(func->arguments().first())) {
                secondArg.prepend('(');
                secondArg.append(')');
            }

            if (func->isUnaryOperator())
                std::swap(firstArg, secondArg);

            QString op = func->originalName();
            op = op.right(op.size() - (sizeof("operator")/sizeof(char)-1));

            if (func->isBinaryOperator()) {
                if (func->isReverseOperator())
                    std::swap(firstArg, secondArg);

                if (((op == "++") || (op == "--")) && !func->isReverseOperator())  {
                    s << endl << INDENT << "for(int i=0; i < " << secondArg << "; i++, " << firstArg << op << ");" << endl;
                    mc << firstArg;
                } else {
                    mc << firstArg << ' ' << op << ' ' << secondArg;
                }
            } else {
                mc << op << ' ' << secondArg;
            }
        } else if (!injectedCodeCallsCppFunction(func)) {
            if (func->isConstructor() || func->isCopyConstructor()) {
                isCtor = true;
                QString className = wrapperName(func->ownerClass());

                if (func->isCopyConstructor() && maxArgs == 1) {
                    mc << "new ::" << className << '(' << CPP_ARG0 << ')';
                } else {
                    QString ctorCall = className + '(' + userArgs.join(", ") + ')';
                    if (usePySideExtensions() && func->ownerClass()->isQObject()) {
                        s << INDENT << "void* addr = PySide::nextQObjectMemoryAddr();" << endl;
                        mc << "addr ? new (addr) ::" << ctorCall << " : new ::" << ctorCall;
                    } else {
                        mc << "new ::" << ctorCall;
                    }
                }

            } else {
                if (func->ownerClass()) {
                    if (!avoidProtectedHack() || !func->isProtected()) {
                        if (func->isStatic()) {
                            mc << func->ownerClass()->qualifiedCppName() << "::";
                        } else {
                            if (func->isConstant()) {
                                if (avoidProtectedHack()) {
                                    mc << "const_cast<const ::";
                                    if (func->ownerClass()->hasProtectedMembers())
                                        mc << wrapperName(func->ownerClass());
                                    else
                                        mc << func->ownerClass()->qualifiedCppName();
                                    mc <<  "*>(" CPP_SELF_VAR ")->";
                                } else {
                                    mc << "const_cast<const ::" << func->ownerClass()->qualifiedCppName();
                                    mc <<  "*>(" CPP_SELF_VAR ")->";
                                }
                            } else {
                                mc << CPP_SELF_VAR "->";
                            }
                        }

                        if (!func->isAbstract() && func->isVirtual())
                            mc << "::%CLASS_NAME::";

                        mc << func->originalName();
                    } else {
                        if (!func->isStatic())
                            mc << "((" << wrapperName(func->ownerClass()) << "*) " << CPP_SELF_VAR << ")->";

                        if (!func->isAbstract())
                            mc << (func->isProtected() ? wrapperName(func->ownerClass()) : "::" + func->ownerClass()->qualifiedCppName()) << "::";
                        mc << func->originalName() << "_protected";
                    }
                } else {
                    mc << func->originalName();
                }
                mc << '(' << userArgs.join(", ") << ')';
                if (!func->isAbstract() && func->isVirtual()) {
                    mc.flush();
                    if (!avoidProtectedHack() || !func->isProtected()) {
                        QString virtualCall(methodCall);
                        QString normalCall(methodCall);
                        virtualCall = virtualCall.replace("%CLASS_NAME", func->ownerClass()->qualifiedCppName());
                        normalCall = normalCall.replace("::%CLASS_NAME::", "");
                        methodCall = "";
                        mc << "Shiboken::Object::hasCppWrapper(reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR ")) ? ";
                        mc << virtualCall << " : " <<  normalCall;
                    }
                }
            }
        }

        if (!injectedCodeCallsCppFunction(func)) {
            s << INDENT << BEGIN_ALLOW_THREADS << endl << INDENT;
            if (isCtor) {
                s << "cptr = ";
            } else if (func->type() && !func->isInplaceOperator()) {
                bool writeReturnType = true;
                if (avoidProtectedHack()) {
                    const AbstractMetaEnum* metaEnum = findAbstractMetaEnum(func->type());
                    if (metaEnum) {
                        QString enumName;
                        if (metaEnum->isProtected())
                            enumName = protectedEnumSurrogateName(metaEnum);
                        else
                            enumName = func->type()->cppSignature();
                        methodCall.prepend(enumName + '(');
                        methodCall.append(')');
                        s << enumName;
                        writeReturnType = false;
                    }
                }
                if (writeReturnType)
                    s << func->type()->cppSignature();
                s << " " CPP_RETURN_VAR " = ";
            }
            s << methodCall << ';' << endl;
            s << INDENT << END_ALLOW_THREADS << endl;

            if (!func->conversionRule(TypeSystem::TargetLangCode, 0).isEmpty()) {
                writeConversionRule(s, func, TypeSystem::TargetLangCode, PYTHON_RETURN_VAR);
            } else if (!isCtor && !func->isInplaceOperator() && func->type()
                && !injectedCodeHasReturnValueAttribution(func, TypeSystem::TargetLangCode)) {
                s << INDENT << PYTHON_RETURN_VAR " = ";
                writeToPythonConversion(s, func->type(), func->ownerClass(), CPP_RETURN_VAR);
                s << ';' << endl;
            }
        }
    }

    if (func->hasInjectedCode() && !func->isConstructor()) {
        s << endl;
        writeCodeSnips(s, snips, CodeSnip::End, TypeSystem::TargetLangCode, func, lastArg);
    }

    bool hasReturnPolicy = false;

    // Ownership transference between C++ and Python.
    QList<ArgumentModification> ownership_mods;
    // Python object reference management.
    QList<ArgumentModification> refcount_mods;
    foreach (FunctionModification func_mod, func->modifications()) {
        foreach (ArgumentModification arg_mod, func_mod.argument_mods) {
            if (!arg_mod.ownerships.isEmpty() && arg_mod.ownerships.contains(TypeSystem::TargetLangCode))
                ownership_mods.append(arg_mod);
            else if (!arg_mod.referenceCounts.isEmpty())
                refcount_mods.append(arg_mod);
        }
    }

    // If there's already a setParent(return, me), don't use the return heuristic!
    if (func->argumentOwner(func->ownerClass(), -1).index == 0)
        hasReturnPolicy = true;

    if (!ownership_mods.isEmpty()) {
        s << endl << INDENT << "// Ownership transferences." << endl;
        foreach (ArgumentModification arg_mod, ownership_mods) {
            const AbstractMetaClass* wrappedClass = 0;
            QString pyArgName = argumentNameFromIndex(func, arg_mod.index, &wrappedClass);
            if (!wrappedClass) {
                s << "#error Invalid ownership modification for argument " << arg_mod.index << '(' << pyArgName << ')' << endl << endl;
                break;
            }

            if (arg_mod.index == 0 || arg_mod.owner.index == 0)
                hasReturnPolicy = true;

            // The default ownership does nothing. This is useful to avoid automatic heuristically
            // based generation of code defining parenting.
            if (arg_mod.ownerships[TypeSystem::TargetLangCode] == TypeSystem::DefaultOwnership)
                continue;

            s << INDENT;
            if (arg_mod.ownerships[TypeSystem::TargetLangCode] == TypeSystem::TargetLangOwnership) {
                s << "Shiboken::Object::getOwnership(" << pyArgName << ");";
            } else if (wrappedClass->hasVirtualDestructor()) {
                if (arg_mod.index == 0) {
                    s << "Shiboken::Object::releaseOwnership(" PYTHON_RETURN_VAR ");";
                } else {
                    s << "Shiboken::Object::releaseOwnership(" << pyArgName << ");";
                }
            } else {
                s << "Shiboken::Object::invalidate(" << pyArgName << ");";
            }
            s << endl;
        }

    } else if (!refcount_mods.isEmpty()) {
        foreach (ArgumentModification arg_mod, refcount_mods) {
            ReferenceCount refCount = arg_mod.referenceCounts.first();
            if (refCount.action != ReferenceCount::Set
                && refCount.action != ReferenceCount::Remove
                && refCount.action != ReferenceCount::Add) {
                ReportHandler::warning("\"set\", \"add\" and \"remove\" are the only values supported by Shiboken for action attribute of reference-count tag.");
                continue;
            }
            const AbstractMetaClass* wrappedClass = 0;

            QString pyArgName;
            if (refCount.action == ReferenceCount::Remove) {
                pyArgName = "Py_None";
            } else {
                pyArgName = argumentNameFromIndex(func, arg_mod.index, &wrappedClass);
                if (pyArgName.isEmpty()) {
                    s << "#error Invalid reference count modification for argument " << arg_mod.index << endl << endl;
                    break;
                }
            }

            if (refCount.action == ReferenceCount::Add || refCount.action == ReferenceCount::Set)
                s << INDENT << "Shiboken::Object::keepReference(";
            else
                s << INDENT << "Shiboken::Object::removeReference(";

            s << "reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR "), \"";
            QString varName = arg_mod.referenceCounts.first().varName;
            if (varName.isEmpty())
                varName = func->minimalSignature() + QString().number(arg_mod.index);

            s << varName << "\", " << pyArgName
              << (refCount.action == ReferenceCount::Add ? ", true" : "")
              << ");" << endl;

            if (arg_mod.index == 0)
                hasReturnPolicy = true;
        }
    }
    writeParentChildManagement(s, func, !hasReturnPolicy);
}

QStringList CppGenerator::getAncestorMultipleInheritance(const AbstractMetaClass* metaClass)
{
    QStringList result;
    AbstractMetaClassList baseClases = getBaseClasses(metaClass);
    if (!baseClases.isEmpty()) {
        foreach (const AbstractMetaClass* baseClass, baseClases) {
            result.append(QString("((size_t) static_cast<const %1*>(class_ptr)) - base").arg(baseClass->qualifiedCppName()));
            result.append(QString("((size_t) static_cast<const %1*>((%2*)((void*)class_ptr))) - base").arg(baseClass->qualifiedCppName()).arg(metaClass->qualifiedCppName()));
        }
        foreach (const AbstractMetaClass* baseClass, baseClases)
            result.append(getAncestorMultipleInheritance(baseClass));
    }
    return result;
}

void CppGenerator::writeMultipleInheritanceInitializerFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString className = metaClass->qualifiedCppName();
    QStringList ancestors = getAncestorMultipleInheritance(metaClass);
    s << "static int mi_offsets[] = { ";
    for (int i = 0; i < ancestors.size(); i++)
        s << "-1, ";
    s << "-1 };" << endl;
    s << "int*" << endl;
    s << multipleInheritanceInitializerFunctionName(metaClass) << "(const void* cptr)" << endl;
    s << '{' << endl;
    s << INDENT << "if (mi_offsets[0] == -1) {" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "std::set<int> offsets;" << endl;
        s << INDENT << "std::set<int>::iterator it;" << endl;
        s << INDENT << "const " << className << "* class_ptr = reinterpret_cast<const " << className << "*>(cptr);" << endl;
        s << INDENT << "size_t base = (size_t) class_ptr;" << endl;

        foreach (QString ancestor, ancestors)
            s << INDENT << "offsets.insert(" << ancestor << ");" << endl;

        s << endl;
        s << INDENT << "offsets.erase(0);" << endl;
        s << endl;

        s << INDENT << "int i = 0;" << endl;
        s << INDENT << "for (it = offsets.begin(); it != offsets.end(); it++) {" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "mi_offsets[i] = *it;" << endl;
            s << INDENT << "i++;" << endl;
        }
        s << INDENT << '}' << endl;
    }
    s << INDENT << '}' << endl;
    s << INDENT << "return mi_offsets;" << endl;
    s << '}' << endl;
}

void CppGenerator::writeSpecialCastFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString className = metaClass->qualifiedCppName();
    s << "static void* " << cpythonSpecialCastFunctionName(metaClass) << "(void* obj, SbkObjectType* desiredType)\n";
    s << "{\n";
    s << INDENT << className << "* me = reinterpret_cast< ::" << className << "*>(obj);\n";
    bool firstClass = true;
    foreach (const AbstractMetaClass* baseClass, getAllAncestors(metaClass)) {
        s << INDENT << (!firstClass ? "else " : "") << "if (desiredType == reinterpret_cast<SbkObjectType*>(" << cpythonTypeNameExt(baseClass->typeEntry()) << "))\n";
        Indentation indent(INDENT);
        s << INDENT << "return static_cast< ::" << baseClass->qualifiedCppName() << "*>(me);\n";
        firstClass = false;
    }
    s << INDENT << "return me;\n";
    s << "}\n\n";
}

void CppGenerator::writeExtendedIsConvertibleFunction(QTextStream& s, const TypeEntry* externalType, const QList<const AbstractMetaClass*>& conversions)
{
    s << "static bool " << extendedIsConvertibleFunctionName(externalType) << "(PyObject* pyobj)" << endl;
    s << '{' << endl;
    s << INDENT << "return ";
    bool isFirst = true;
    foreach (const AbstractMetaClass* metaClass, conversions) {
        Indentation indent(INDENT);
        if (isFirst)
            isFirst = false;
        else
            s << endl << INDENT << " || ";
        s << cpythonIsConvertibleFunction(metaClass->typeEntry()) << "(pyobj)";
    }
    s << ';' << endl;
    s << '}' << endl;
}

void CppGenerator::writeExtendedToCppFunction(QTextStream& s, const TypeEntry* externalType, const QList<const AbstractMetaClass*>& conversions)
{
    s << "static void* " << extendedToCppFunctionName(externalType) << "(PyObject* pyobj)" << endl;
    s << '{' << endl;
    s << INDENT << "void* cptr = 0;" << endl;
    bool isFirst = true;
    foreach (const AbstractMetaClass* metaClass, conversions) {
        s << INDENT;
        if (isFirst)
            isFirst = false;
        else
            s << "else ";
        s << "if (" << cpythonIsConvertibleFunction(metaClass->typeEntry()) << "(pyobj))" << endl;
        Indentation indent(INDENT);
        s << INDENT << "cptr = new " << externalType->name() << '(';
        writeToCppConversion(s, metaClass, "pyobj");
        s << ");" << endl;
    }
    s << INDENT << "return cptr;" << endl;
    s << '}' << endl;
}

void CppGenerator::writeExtendedConverterInitialization(QTextStream& s, const TypeEntry* externalType, const QList<const AbstractMetaClass*>& conversions)
{
    s << INDENT << "// Extended implicit conversions for " << externalType->targetLangPackage() << '.' << externalType->name() << endl;
    s << INDENT << "shiboType = reinterpret_cast<SbkObjectType*>(";
    s << cppApiVariableName(externalType->targetLangPackage()) << '[';
    s << getTypeIndexVariableName(externalType) << "]);" << endl;
    s << INDENT << "Shiboken::ObjectType::setExternalIsConvertibleFunction(shiboType, " << extendedIsConvertibleFunctionName(externalType) << ");" << endl;
    s << INDENT << "Shiboken::ObjectType::setExternalCppConversionFunction(shiboType, " << extendedToCppFunctionName(externalType) << ");" << endl;
}

QString CppGenerator::multipleInheritanceInitializerFunctionName(const AbstractMetaClass* metaClass)
{
    if (!hasMultipleInheritanceInAncestry(metaClass))
        return QString();
    return QString("%1_mi_init").arg(cpythonBaseName(metaClass->typeEntry()));
}

bool CppGenerator::supportsMappingProtocol(const AbstractMetaClass* metaClass)
{
    foreach(QString funcName, m_mappingProtocol.keys()) {
        if (metaClass->hasFunction(funcName))
            return true;
    }

    return false;
}

bool CppGenerator::supportsNumberProtocol(const AbstractMetaClass* metaClass)
{
    return metaClass->hasArithmeticOperatorOverload()
            || metaClass->hasLogicalOperatorOverload()
            || metaClass->hasBitwiseOperatorOverload()
            || hasBoolCast(metaClass);
}

bool CppGenerator::supportsSequenceProtocol(const AbstractMetaClass* metaClass)
{
    foreach(QString funcName, m_sequenceProtocol.keys()) {
        if (metaClass->hasFunction(funcName))
            return true;
    }

    const ComplexTypeEntry* baseType = metaClass->typeEntry()->baseContainerType();
    if (baseType && baseType->isContainer())
        return true;

    return false;
}

bool CppGenerator::shouldGenerateGetSetList(const AbstractMetaClass* metaClass)
{
    foreach (AbstractMetaField* f, metaClass->fields()) {
        if (!f->isStatic())
            return true;
    }
    return false;
}

void CppGenerator::writeClassDefinition(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString tp_flags;
    QString tp_init;
    QString tp_new;
    QString tp_dealloc;
    QString tp_hash('0');
    QString tp_call('0');
    QString cppClassName = metaClass->qualifiedCppName();
    QString className = cpythonTypeName(metaClass).replace(QRegExp("_Type$"), "");
    QString baseClassName('0');
    AbstractMetaFunctionList ctors;
    foreach (AbstractMetaFunction* f, metaClass->queryFunctions(AbstractMetaClass::Constructors)) {
        if (!f->isPrivate() && !f->isModifiedRemoved())
            ctors.append(f);
    }

    if (!metaClass->baseClass())
        baseClassName = "reinterpret_cast<PyTypeObject*>(&SbkObject_Type)";

    bool onlyPrivCtor = !metaClass->hasNonPrivateConstructor();

    if (metaClass->isNamespace() || metaClass->hasPrivateDestructor()) {
        tp_flags = "Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC";
        tp_dealloc = metaClass->hasPrivateDestructor() ?
                     "SbkDeallocWrapperWithPrivateDtor" : "0";
        tp_init = "0";
    } else {
        if (onlyPrivCtor)
            tp_flags = "Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC";
        else
            tp_flags = "Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC";

        QString deallocClassName;
        if (shouldGenerateCppWrapper(metaClass))
            deallocClassName = wrapperName(metaClass);
        else
            deallocClassName = cppClassName;
        tp_dealloc = "&SbkDeallocWrapper";
        tp_init = onlyPrivCtor || ctors.isEmpty() ? "0" : cpythonFunctionName(ctors.first());
    }

    QString tp_getattro('0');
    QString tp_setattro('0');
    if (usePySideExtensions() && (metaClass->qualifiedCppName() == "QObject")) {
        tp_getattro = cpythonGetattroFunctionName(metaClass);
        tp_setattro = cpythonSetattroFunctionName(metaClass);
    } else if (classNeedsGetattroFunction(metaClass)) {
        tp_getattro = cpythonGetattroFunctionName(metaClass);
    }

    if (metaClass->hasPrivateDestructor() || onlyPrivCtor)
        tp_new = "0";
    else
        tp_new = "SbkObjectTpNew";

    QString tp_richcompare = QString('0');
    if (metaClass->hasComparisonOperatorOverload())
        tp_richcompare = cpythonBaseName(metaClass) + "_richcompare";

    QString tp_getset = QString('0');
    if (shouldGenerateGetSetList(metaClass))
        tp_getset = cpythonGettersSettersDefinitionName(metaClass);

    // search for special functions
    ShibokenGenerator::clearTpFuncs();
    foreach (AbstractMetaFunction* func, metaClass->functions()) {
        if (m_tpFuncs.contains(func->name()))
            m_tpFuncs[func->name()] = cpythonFunctionName(func);
    }
    if (m_tpFuncs["__repr__"] == "0"
        && !metaClass->isQObject()
        && metaClass->hasToStringCapability()) {
        m_tpFuncs["__repr__"] = writeReprFunction(s, metaClass);
    }

    // class or some ancestor has multiple inheritance
    const AbstractMetaClass* miClass = getMultipleInheritingClass(metaClass);
    if (miClass) {
        if (metaClass == miClass)
            writeMultipleInheritanceInitializerFunction(s, metaClass);
        writeSpecialCastFunction(s, metaClass);
        s << endl;
    }

    if (!metaClass->typeEntry()->hashFunction().isEmpty())
        tp_hash = '&' + cpythonBaseName(metaClass) + "_HashFunc";

    const AbstractMetaFunction* callOp = metaClass->findFunction("operator()");
    if (callOp && !callOp->isModifiedRemoved())
        tp_call = '&' + cpythonFunctionName(callOp);


    s << "// Class Definition -----------------------------------------------" << endl;
    s << "extern \"C\" {" << endl;
    s << "static SbkObjectType " << className + "_Type" << " = { { {" << endl;
    s << INDENT << "PyVarObject_HEAD_INIT(&SbkObjectType_Type, 0)" << endl;
    s << INDENT << "/*tp_name*/             \"" << getClassTargetFullName(metaClass) << "\"," << endl;
    s << INDENT << "/*tp_basicsize*/        sizeof(SbkObject)," << endl;
    s << INDENT << "/*tp_itemsize*/         0," << endl;
    s << INDENT << "/*tp_dealloc*/          " << tp_dealloc << ',' << endl;
    s << INDENT << "/*tp_print*/            0," << endl;
    s << INDENT << "/*tp_getattr*/          0," << endl;
    s << INDENT << "/*tp_setattr*/          0," << endl;
    s << INDENT << "/*tp_compare*/          0," << endl;
    s << INDENT << "/*tp_repr*/             " << m_tpFuncs["__repr__"] << "," << endl;
    s << INDENT << "/*tp_as_number*/        0," << endl;
    s << INDENT << "/*tp_as_sequence*/      0," << endl;
    s << INDENT << "/*tp_as_mapping*/       0," << endl;
    s << INDENT << "/*tp_hash*/             " << tp_hash << ',' << endl;
    s << INDENT << "/*tp_call*/             " << tp_call << ',' << endl;
    s << INDENT << "/*tp_str*/              " << m_tpFuncs["__str__"] << ',' << endl;
    s << INDENT << "/*tp_getattro*/         " << tp_getattro << ',' << endl;
    s << INDENT << "/*tp_setattro*/         " << tp_setattro << ',' << endl;
    s << INDENT << "/*tp_as_buffer*/        0," << endl;
    s << INDENT << "/*tp_flags*/            " << tp_flags << ',' << endl;
    s << INDENT << "/*tp_doc*/              0," << endl;
    s << INDENT << "/*tp_traverse*/         " << className << "_traverse," << endl;
    s << INDENT << "/*tp_clear*/            " << className << "_clear," << endl;
    s << INDENT << "/*tp_richcompare*/      " << tp_richcompare << ',' << endl;
    s << INDENT << "/*tp_weaklistoffset*/   0," << endl;
    s << INDENT << "/*tp_iter*/             " << m_tpFuncs["__iter__"] << ',' << endl;
    s << INDENT << "/*tp_iternext*/         " << m_tpFuncs["__next__"] << ',' << endl;
    s << INDENT << "/*tp_methods*/          " << className << "_methods," << endl;
    s << INDENT << "/*tp_members*/          0," << endl;
    s << INDENT << "/*tp_getset*/           " << tp_getset << ',' << endl;
    s << INDENT << "/*tp_base*/             " << baseClassName << ',' << endl;
    s << INDENT << "/*tp_dict*/             0," << endl;
    s << INDENT << "/*tp_descr_get*/        0," << endl;
    s << INDENT << "/*tp_descr_set*/        0," << endl;
    s << INDENT << "/*tp_dictoffset*/       0," << endl;
    s << INDENT << "/*tp_init*/             " << tp_init << ',' << endl;
    s << INDENT << "/*tp_alloc*/            0," << endl;
    s << INDENT << "/*tp_new*/              " << tp_new << ',' << endl;
    s << INDENT << "/*tp_free*/             0," << endl;
    s << INDENT << "/*tp_is_gc*/            0," << endl;
    s << INDENT << "/*tp_bases*/            0," << endl;
    s << INDENT << "/*tp_mro*/              0," << endl;
    s << INDENT << "/*tp_cache*/            0," << endl;
    s << INDENT << "/*tp_subclasses*/       0," << endl;
    s << INDENT << "/*tp_weaklist*/         0" << endl;
    s << "}, }," << endl;
    s << INDENT << "/*priv_data*/           0" << endl;
    s << "};" << endl;
    QString suffix;
    if (isObjectType(metaClass))
        suffix = "*";
    s << "} //extern"  << endl;
}

void CppGenerator::writeMappingMethods(QTextStream& s, const AbstractMetaClass* metaClass)
{

    QMap<QString, QString> funcs;

    QHash< QString, QPair< QString, QString > >::const_iterator it = m_mappingProtocol.begin();
    for (; it != m_mappingProtocol.end(); ++it) {
        const AbstractMetaFunction* func = metaClass->findFunction(it.key());
        if (!func)
            continue;
        QString funcName = cpythonFunctionName(func);
        QString funcArgs = it.value().first;
        QString funcRetVal = it.value().second;

        CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, TypeSystem::TargetLangCode);
        s << funcRetVal << ' ' << funcName << '(' << funcArgs << ')' << endl << '{' << endl;
        writeInvalidPyObjectCheck(s, PYTHON_SELF_VAR);

        writeCppSelfDefinition(s, func);

        const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
        writeCodeSnips(s, snips, CodeSnip::Any, TypeSystem::TargetLangCode, func, lastArg);
        s << '}' << endl << endl;
    }
}

void CppGenerator::writeSequenceMethods(QTextStream& s, const AbstractMetaClass* metaClass)
{

    QMap<QString, QString> funcs;
    bool injectedCode = false;

    QHash< QString, QPair< QString, QString > >::const_iterator it = m_sequenceProtocol.begin();
    for (; it != m_sequenceProtocol.end(); ++it) {
        const AbstractMetaFunction* func = metaClass->findFunction(it.key());
        if (!func)
            continue;
        injectedCode = true;
        QString funcName = cpythonFunctionName(func);
        QString funcArgs = it.value().first;
        QString funcRetVal = it.value().second;

        CodeSnipList snips = func->injectedCodeSnips(CodeSnip::Any, TypeSystem::TargetLangCode);
        s << funcRetVal << ' ' << funcName << '(' << funcArgs << ')' << endl << '{' << endl;
        writeInvalidPyObjectCheck(s, PYTHON_SELF_VAR);

        writeCppSelfDefinition(s, func);

        const AbstractMetaArgument* lastArg = func->arguments().isEmpty() ? 0 : func->arguments().last();
        writeCodeSnips(s, snips,CodeSnip::Any, TypeSystem::TargetLangCode, func, lastArg);
        s << '}' << endl << endl;
    }

    if (!injectedCode)
        writeStdListWrapperMethods(s, metaClass);
}

void CppGenerator::writeTypeAsSequenceDefinition(QTextStream& s, const AbstractMetaClass* metaClass)
{
    bool hasFunctions = false;
    QMap<QString, QString> funcs;
    foreach(QString funcName, m_sequenceProtocol.keys()) {
        const AbstractMetaFunction* func = metaClass->findFunction(funcName);
        funcs[funcName] = func ? cpythonFunctionName(func).prepend("&") : QString();
        if (!hasFunctions && func)
            hasFunctions = true;
    }

    QString baseName = cpythonBaseName(metaClass);

    //use default implementation
    if (!hasFunctions) {
        funcs["__len__"] = baseName + "__len__";
        funcs["__getitem__"] = baseName + "__getitem__";
        funcs["__setitem__"] = baseName + "__setitem__";
    }

    s << INDENT << "memset(&" << baseName << "_Type.super.as_sequence, 0, sizeof(PySequenceMethods));" << endl;
    foreach (const QString& sqName, m_sqFuncs.keys()) {
        if (funcs[sqName].isEmpty())
            continue;
        if (m_sqFuncs[sqName] == "sq_slice")
            s << "#ifndef IS_PY3K" << endl;
        s << INDENT << baseName << "_Type.super.as_sequence." << m_sqFuncs[sqName] << " = " << funcs[sqName] << ';' << endl;
        if (m_sqFuncs[sqName] == "sq_slice")
            s << "#endif" << endl;
    }
}

void CppGenerator::writeTypeAsMappingDefinition(QTextStream& s, const AbstractMetaClass* metaClass)
{
    bool hasFunctions = false;
    QMap<QString, QString> funcs;
    foreach(QString funcName, m_mappingProtocol.keys()) {
        const AbstractMetaFunction* func = metaClass->findFunction(funcName);
        funcs[funcName] = func ? cpythonFunctionName(func).prepend("&") : "0";
        if (!hasFunctions && func)
            hasFunctions = true;
    }

    //use default implementation
    if (!hasFunctions) {
        funcs["__mlen__"] = QString();
        funcs["__mgetitem__"] = QString();
        funcs["__msetitem__"] = QString();
    }

    QString baseName = cpythonBaseName(metaClass);
    s << INDENT << "memset(&" << baseName << "_Type.super.as_mapping, 0, sizeof(PyMappingMethods));" << endl;
    foreach (const QString& mpName, m_mpFuncs.keys()) {
        if (funcs[mpName].isEmpty())
            continue;
        s << INDENT << baseName << "_Type.super.as_mapping." << m_mpFuncs[mpName] << " = " << funcs[mpName] << ';' << endl;
    }
}

void CppGenerator::writeTypeAsNumberDefinition(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QMap<QString, QString> nb;

    nb["__add__"] = QString();
    nb["__sub__"] = QString();
    nb["__mul__"] = QString();
    nb["__div__"] = QString();
    nb["__mod__"] = QString();
    nb["__neg__"] = QString();
    nb["__pos__"] = QString();
    nb["__invert__"] = QString();
    nb["__lshift__"] = QString();
    nb["__rshift__"] = QString();
    nb["__and__"] = QString();
    nb["__xor__"] = QString();
    nb["__or__"] = QString();
    nb["__iadd__"] = QString();
    nb["__isub__"] = QString();
    nb["__imul__"] = QString();
    nb["__idiv__"] = QString();
    nb["__imod__"] = QString();
    nb["__ilshift__"] = QString();
    nb["__irshift__"] = QString();
    nb["__iand__"] = QString();
    nb["__ixor__"] = QString();
    nb["__ior__"] = QString();

    QList<AbstractMetaFunctionList> opOverloads =
            filterGroupedOperatorFunctions(metaClass,
                                           AbstractMetaClass::ArithmeticOp
                                           | AbstractMetaClass::LogicalOp
                                           | AbstractMetaClass::BitwiseOp);

    foreach (AbstractMetaFunctionList opOverload, opOverloads) {
        const AbstractMetaFunction* rfunc = opOverload[0];
        QString opName = ShibokenGenerator::pythonOperatorFunctionName(rfunc);
        nb[opName] = cpythonFunctionName(rfunc);
    }

    QString baseName = cpythonBaseName(metaClass);

    nb["bool"] = hasBoolCast(metaClass) ? baseName + "___nb_bool" : QString();

    s << INDENT << "memset(&" << baseName << "_Type.super.as_number, 0, sizeof(PyNumberMethods));" << endl;
    foreach (const QString& nbName, m_nbFuncs.keys()) {
        if (nb[nbName].isEmpty())
            continue;

        // bool is special because the field name differs on Python 2 and 3 (nb_nonzero vs nb_bool)
        // so a shiboken macro is used.
        if (nbName == "bool") {
            s << INDENT << "SBK_NB_BOOL(" << baseName << "_Type.super.as_number) = " << nb[nbName] << ';' << endl;
        } else {
            bool excludeFromPy3K = nbName == "__div__" || nbName == "__idiv__";
            if (excludeFromPy3K)
                s << "#ifndef IS_PY3K" << endl;
            s << INDENT << baseName << "_Type.super.as_number." << m_nbFuncs[nbName] << " = " << nb[nbName] << ';' << endl;
            if (excludeFromPy3K)
                s << "#endif" << endl;
        }
    }
    if (!nb["__div__"].isEmpty())
        s << INDENT << baseName << "_Type.super.as_number.nb_true_divide = " << nb["__div__"] << ';' << endl;
}

void CppGenerator::writeTpTraverseFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString baseName = cpythonBaseName(metaClass);
    s << "static int ";
    s << baseName << "_traverse(PyObject* " PYTHON_SELF_VAR ", visitproc visit, void* arg)" << endl;
    s << '{' << endl;
    s << INDENT << "return reinterpret_cast<PyTypeObject*>(&SbkObject_Type)->tp_traverse(" PYTHON_SELF_VAR ", visit, arg);" << endl;
    s << '}' << endl;
}

void CppGenerator::writeTpClearFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString baseName = cpythonBaseName(metaClass);
    s << "static int ";
    s << baseName << "_clear(PyObject* " PYTHON_SELF_VAR ")" << endl;
    s << '{' << endl;
    s << INDENT << "return reinterpret_cast<PyTypeObject*>(&SbkObject_Type)->tp_clear(" PYTHON_SELF_VAR ");" << endl;
    s << '}' << endl;
}

void CppGenerator::writeCopyFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString className = cpythonTypeName(metaClass).replace(QRegExp("_Type$"), "");
    s << "static PyObject* " << className << "___copy__(PyObject* " PYTHON_SELF_VAR ")" << endl;
    s << "{" << endl;
    writeCppSelfDefinition(s, metaClass, false, true);
    s << INDENT << "PyObject* " << PYTHON_RETURN_VAR << " = " << cpythonToPythonConversionFunction(metaClass);
    s << "(" CPP_SELF_VAR ");" << endl;
    writeFunctionReturnErrorCheckSection(s);
    s << INDENT << "return " PYTHON_RETURN_VAR ";" << endl;
    s << "}" << endl;
    s << endl;
}

void CppGenerator::writeGetterFunction(QTextStream& s, const AbstractMetaField* metaField)
{
    ErrorCode errorCode(0);
    s << "static PyObject* " << cpythonGetterFunctionName(metaField) << "(PyObject* " PYTHON_SELF_VAR ", void*)" << endl;
    s << '{' << endl;

    writeCppSelfDefinition(s, metaField->enclosingClass());

    AbstractMetaType* fieldType = metaField->type();
    // Force use of pointer to return internal variable memory
    bool newWrapperSameObject = !fieldType->isConstant() && isWrapperType(fieldType) && !isPointer(fieldType);

    QString cppField;
    if (avoidProtectedHack() && metaField->isProtected()) {
        cppField = QString("((%1*)%2)->%3()").arg(wrapperName(metaField->enclosingClass()))
                                             .arg(CPP_SELF_VAR)
                                             .arg(protectedFieldGetterName(metaField));
    } else {
        cppField = QString("%2->%3").arg(CPP_SELF_VAR).arg(metaField->name());
        if (newWrapperSameObject) {
            cppField.prepend("&(");
            cppField.append(')');
        }
    }

    s << INDENT << "PyObject* value = ";
    if (newWrapperSameObject) {
        s << "Shiboken::Object::newObject((SbkObjectType*)" << cpythonTypeNameExt(fieldType);
        s << ", " << cppField << ", false, true);" << endl;
        s << INDENT << "Shiboken::Object::setParent(" PYTHON_SELF_VAR ", value)";
    } else {
        writeToPythonConversion(s, fieldType, metaField->enclosingClass(), cppField);
    }
    s << ';' << endl;

    s << INDENT << "return value;" << endl;
    s << '}' << endl;
}

void CppGenerator::writeSetterFunction(QTextStream& s, const AbstractMetaField* metaField)
{
    ErrorCode errorCode(0);
    s << "static int " << cpythonSetterFunctionName(metaField) << "(PyObject* " PYTHON_SELF_VAR ", PyObject* value, void*)" << endl;
    s << '{' << endl;

    writeCppSelfDefinition(s, metaField->enclosingClass());

    s << INDENT << "if (value == 0) {" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "PyErr_SetString(PyExc_TypeError, \"'";
        s << metaField->name() << "' may not be deleted\");" << endl;
        s << INDENT << "return -1;" << endl;
    }
    s << INDENT << '}' << endl;

    AbstractMetaType* fieldType = metaField->type();

    s << INDENT << "if (!";
    writeTypeCheck(s, fieldType, "value", isNumber(fieldType->typeEntry()));
    s << ") {" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "PyErr_SetString(PyExc_TypeError, \"wrong type attributed to '";
        s << metaField->name() << "', '" << fieldType->name() << "' or convertible type expected\");" << endl;
        s << INDENT << "return -1;" << endl;
    }
    s << INDENT << '}' << endl << endl;

    QString conversion;
    QTextStream c(&conversion);
    writeToCppConversion(c, fieldType, metaField->enclosingClass(), "value");

    if (avoidProtectedHack() && metaField->isProtected()) {
        conversion = QString("((%1*)%2)->%3(%4)").arg(wrapperName(metaField->enclosingClass()))
                                                 .arg(CPP_SELF_VAR)
                                                 .arg(protectedFieldSetterName(metaField))
                                                 .arg(conversion);
    } else {
        conversion = QString("%1->%2 = %3").arg(CPP_SELF_VAR).arg(metaField->name()).arg(conversion);
    }

    s << INDENT << conversion << ';' << endl << endl;

    if (isPointerToWrapperType(fieldType)) {
        s << INDENT << "Shiboken::Object::keepReference(reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR "), \"";
        s << metaField->name() << "\", value);" << endl;
    }

    s << INDENT << "return 0;" << endl;
    s << '}' << endl;
}

void CppGenerator::writeRichCompareFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString baseName = cpythonBaseName(metaClass);
    s << "static PyObject* ";
    s << baseName << "_richcompare(PyObject* " PYTHON_SELF_VAR ", PyObject* " PYTHON_ARG ", int op)" << endl;
    s << '{' << endl;
    writeCppSelfDefinition(s, metaClass, false, true);
    writeUnusedVariableCast(s, CPP_SELF_VAR);
    s << INDENT << "PyObject* " PYTHON_RETURN_VAR " = 0;" << endl;
    s << endl;

    s << INDENT << "switch (op) {" << endl;
    {
        Indentation indent(INDENT);
        foreach (AbstractMetaFunctionList overloads, filterGroupedOperatorFunctions(metaClass, AbstractMetaClass::ComparisonOp)) {
            const AbstractMetaFunction* rfunc = overloads[0];

            QString operatorId = ShibokenGenerator::pythonRichCompareOperatorId(rfunc);
            s << INDENT << "case " << operatorId << ':' << endl;

            Indentation indent(INDENT);

            QString op = rfunc->originalName();
            op = op.right(op.size() - QString("operator").size());

            int alternativeNumericTypes = 0;
            foreach (const AbstractMetaFunction* func, overloads) {
                if (!func->isStatic() &&
                    ShibokenGenerator::isNumber(func->arguments()[0]->type()->typeEntry()))
                    alternativeNumericTypes++;
            }

            bool first = true;
            OverloadData overloadData(overloads, this);
            foreach (OverloadData* data, overloadData.nextOverloadData()) {
                const AbstractMetaFunction* func = data->referenceFunction();
                if (func->isStatic())
                    continue;
                const AbstractMetaType* argType = getArgumentType(func, 1);
                if (!argType)
                    continue;
                bool numberType = alternativeNumericTypes == 1 || ShibokenGenerator::isPyInt(argType);
                if (!first) {
                    s << " else ";
                } else {
                    first = false;
                    s << INDENT;
                }
                s << "if (" << cpythonIsConvertibleFunction(argType, numberType) << "(" PYTHON_ARG ")) {" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "// " << func->signature() << endl;
                    writeArgumentConversion(s, argType, CPP_ARG0, PYTHON_ARG, metaClass, QString(), func->isUserAdded());

                    // If the function is user added, use the inject code
                    if (func->isUserAdded()) {
                        CodeSnipList snips = func->injectedCodeSnips();
                        writeCodeSnips(s, snips, CodeSnip::Any, TypeSystem::TargetLangCode, func, func->arguments().last());
                    } else {
                        QString expression = QString("%1%2 %3 " CPP_ARG0)
                                                .arg(func->isPointerOperator() ? "&" : "")
                                                .arg(CPP_SELF_VAR).arg(op);
                        s << INDENT;
                        if (func->type())
                            s << func->type()->cppSignature() << " " CPP_RETURN_VAR " = ";
                        s << expression << ';' << endl;
                        s << INDENT << PYTHON_RETURN_VAR " = ";
                        if (func->type())
                            writeToPythonConversion(s, func->type(), metaClass, CPP_RETURN_VAR);
                        else
                            s << "Py_None;" << endl << INDENT << "Py_INCREF(Py_None)";
                        s << ';' << endl;
                    }
                }
                s << INDENT << '}';
            }

            s << " else {" << endl;
            if (operatorId == "Py_EQ" || operatorId == "Py_NE") {
                Indentation indent(INDENT);
                s << INDENT << PYTHON_RETURN_VAR " = " << (operatorId == "Py_EQ" ? "Py_False" : "Py_True") << ';' << endl;
                s << INDENT << "Py_INCREF(" PYTHON_RETURN_VAR ");" << endl;
            } else {
                Indentation indent(INDENT);
                s << INDENT << "goto " << baseName << "_RichComparison_TypeError;" << endl;
            }
            s << INDENT << '}' << endl << endl;

            s << INDENT << "break;" << endl;
        }
        s << INDENT << "default:" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "goto " << baseName << "_RichComparison_TypeError;" << endl;
        }
    }
    s << INDENT << '}' << endl << endl;

    s << INDENT << "if (" PYTHON_RETURN_VAR " && !PyErr_Occurred())" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "return " PYTHON_RETURN_VAR ";" << endl;
    }
    s << INDENT << baseName << "_RichComparison_TypeError:" << endl;
    s << INDENT << "PyErr_SetString(PyExc_NotImplementedError, \"operator not implemented.\");" << endl;
    s << INDENT << "return " << m_currentErrorCode << ';' << endl << endl;
    s << '}' << endl << endl;
}

void CppGenerator::writeMethodDefinitionEntry(QTextStream& s, const AbstractMetaFunctionList overloads)
{
    Q_ASSERT(!overloads.isEmpty());
    OverloadData overloadData(overloads, this);
    bool usePyArgs = pythonFunctionWrapperUsesListOfArguments(overloadData);
    const AbstractMetaFunction* func = overloadData.referenceFunction();
    int min = overloadData.minArgs();
    int max = overloadData.maxArgs();

    s << '"' << func->name() << "\", (PyCFunction)" << cpythonFunctionName(func) << ", ";
    if ((min == max) && (max < 2) && !usePyArgs) {
        if (max == 0)
            s << "METH_NOARGS";
        else
            s << "METH_O";
    } else {
        s << "METH_VARARGS";
        if (overloadData.hasArgumentWithDefaultValue())
            s << "|METH_KEYWORDS";
    }
    if (func->ownerClass() && overloadData.hasStaticFunction())
        s << "|METH_STATIC";
}

void CppGenerator::writeMethodDefinition(QTextStream& s, const AbstractMetaFunctionList overloads)
{
    Q_ASSERT(!overloads.isEmpty());
    const AbstractMetaFunction* func = overloads.first();
    if (m_tpFuncs.contains(func->name()))
        return;

    s << INDENT;
    if (OverloadData::hasStaticAndInstanceFunctions(overloads)) {
        s << cpythonMethodDefinitionName(func);
    } else {
        s << '{';
        writeMethodDefinitionEntry(s, overloads);
        s << '}';
    }
    s << ',' << endl;
}

void CppGenerator::writeEnumsInitialization(QTextStream& s, AbstractMetaEnumList& enums)
{
    if (enums.isEmpty())
        return;
    s << INDENT << "// Initialization of enums." << endl << endl;
    foreach (const AbstractMetaEnum* cppEnum, enums) {
        if (cppEnum->isPrivate())
            continue;
        writeEnumInitialization(s, cppEnum);
    }
}

void CppGenerator::writeEnumInitialization(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    const AbstractMetaClass* enclosingClass = getProperEnclosingClassForEnum(cppEnum);
    const AbstractMetaClass* upper = enclosingClass ? enclosingClass->enclosingClass() : 0;
    bool hasUpperEnclosingClass = upper && upper->typeEntry()->codeGeneration() != TypeEntry::GenerateForSubclass;
    QString enclosingObjectVariable;
    if (enclosingClass)
        enclosingObjectVariable = '&' + cpythonTypeName(enclosingClass);
    else if (hasUpperEnclosingClass)
        enclosingObjectVariable = "enclosingClass";
    else
        enclosingObjectVariable = "module";

    s << INDENT << "// Initialization of ";
    s << (cppEnum->isAnonymous() ? "anonymous enum identified by enum value" : "enum");
    s << " '" << cppEnum->name() << "'." << endl;

    if (!cppEnum->isAnonymous()) {
        FlagsTypeEntry* flags = cppEnum->typeEntry()->flags();
        if (flags)
            s << INDENT << cpythonTypeNameExt(flags) << " = &" << cpythonTypeName(flags) << ';' << endl;
        s << INDENT << cpythonTypeNameExt(cppEnum->typeEntry()) << " = Shiboken::Enum::";
        s << ((enclosingClass || hasUpperEnclosingClass) ? "createScopedEnum" : "createGlobalEnum");
        s << '(' << enclosingObjectVariable << ',' << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << '"' << cppEnum->name() << "\"," << endl;
            s << INDENT << '"' << getClassTargetFullName(cppEnum) << "\"," << endl;
            s << INDENT << '"' << (cppEnum->enclosingClass() ? cppEnum->enclosingClass()->qualifiedCppName() + "::" : "");
            s << cppEnum->name() << '"';
            if (flags)
                s << ',' << endl << INDENT << cpythonTypeNameExt(flags);
            s << ");" << endl;
        }
        s << INDENT << "if (!" << cpythonTypeNameExt(cppEnum->typeEntry()) << ')' << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "return " << m_currentErrorCode << ';' << endl << endl;
        }
    }

    foreach (const AbstractMetaEnumValue* enumValue, cppEnum->values()) {
        if (cppEnum->typeEntry()->isEnumValueRejected(enumValue->name()))
            continue;

        QString enumValueText;
        if (!avoidProtectedHack() || !cppEnum->isProtected()) {
            enumValueText = "(long) ";
            if (cppEnum->enclosingClass())
                enumValueText += cppEnum->enclosingClass()->qualifiedCppName() + "::";
            enumValueText += enumValue->name();
        } else {
            enumValueText += QString::number(enumValue->value());
        }

        if (cppEnum->isAnonymous()) {
            if (enclosingClass || hasUpperEnclosingClass) {
                s << INDENT << '{' << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "PyObject* anonEnumItem = PyInt_FromLong(" << enumValueText << ");" << endl;
                    s << INDENT << "if (PyDict_SetItemString(((SbkObjectType*)" << enclosingObjectVariable;
                    s << ")->super.ht_type.tp_dict, \"" << enumValue->name() << "\", anonEnumItem) < 0)" << endl;
                    {
                        Indentation indent(INDENT);
                        s << INDENT << "return " << m_currentErrorCode << ';' << endl;
                    }
                    s << INDENT << "Py_DECREF(anonEnumItem);" << endl;
                }
                s << INDENT << '}' << endl;
            } else {
                s << INDENT << "if (PyModule_AddIntConstant(module, \"" << enumValue->name() << "\", ";
                s << enumValueText << ") < 0)" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "return " << m_currentErrorCode << ';' << endl;
                }
            }
        } else {
            s << INDENT << "if (!Shiboken::Enum::";
            s << ((enclosingClass || hasUpperEnclosingClass) ? "createScopedEnumItem" : "createGlobalEnumItem");
            s << '(' << cpythonTypeNameExt(cppEnum->typeEntry()) << ',' << endl;
            Indentation indent(INDENT);
            s << INDENT << enclosingObjectVariable << ", \"" << enumValue->name() << "\", ";
            s << enumValueText << "))" << endl;
            s << INDENT << "return " << m_currentErrorCode << ';' << endl;
        }
    }

    // TypeResolver stuff
    if (!cppEnum->isAnonymous())
        writeRegisterType(s, cppEnum);

    s << INDENT << "// End of '" << cppEnum->name() << "' enum." << endl << endl;
}

void CppGenerator::writeSignalInitialization(QTextStream& s, const AbstractMetaClass* metaClass)
{
    // Try to check something and print some warnings
    foreach (const AbstractMetaFunction* cppSignal, metaClass->cppSignalFunctions()) {
        if (cppSignal->declaringClass() != metaClass)
            continue;
        foreach (AbstractMetaArgument* arg, cppSignal->arguments()) {
            AbstractMetaType* metaType = arg->type();
            QByteArray origType = SBK_NORMALIZED_TYPE(qPrintable(metaType->originalTypeDescription()));
            QByteArray cppSig = SBK_NORMALIZED_TYPE(qPrintable(metaType->cppSignature()));
            if ((origType != cppSig) && (!metaType->isFlags()))
                ReportHandler::warning("Typedef used on signal " + metaClass->qualifiedCppName() + "::" + cppSignal->signature());
        }
    }

    s << INDENT << "PySide::Signal::registerSignals(&" << cpythonTypeName(metaClass) << ", &::"
                << metaClass->qualifiedCppName() << "::staticMetaObject);" << endl;
}

void CppGenerator::writeFlagsToLong(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    FlagsTypeEntry* flagsEntry = cppEnum->typeEntry()->flags();
    if (!flagsEntry)
        return;
    s << "static PyObject* " << cpythonEnumName(cppEnum) << "_long(PyObject* v)" << endl
      << "{" << endl
      << INDENT << "long val = (long) Shiboken::Converter< ::" << flagsEntry->originalName() << ">::toCpp(v);" << endl
      << INDENT << "return PyLong_FromLong(val);" << endl
      << "}" << endl;
}

void CppGenerator::writeFlagsNonZero(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    FlagsTypeEntry* flagsEntry = cppEnum->typeEntry()->flags();
    if (!flagsEntry)
        return;
    s << "static int " << cpythonEnumName(cppEnum) << "__nonzero(PyObject* v)" << endl
      << "{" << endl
      << INDENT << "long val = (long) Shiboken::Converter< ::" << flagsEntry->originalName() << ">::toCpp(v);" << endl
      << INDENT << "return val != 0;" << endl
      << "}" << endl;
}

void CppGenerator::writeFlagsMethods(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    writeFlagsBinaryOperator(s, cppEnum, "and", "&");
    writeFlagsBinaryOperator(s, cppEnum, "or", "|");
    writeFlagsBinaryOperator(s, cppEnum, "xor", "^");

    writeFlagsUnaryOperator(s, cppEnum, "invert", "~");
    writeFlagsToLong(s, cppEnum);
    writeFlagsNonZero(s, cppEnum);

    s << endl;
}

void CppGenerator::writeFlagsNumberMethodsDefinition(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    QString cpythonName = cpythonEnumName(cppEnum);

    s << "static PyNumberMethods " << cpythonName << "_as_number = {" << endl;
    s << INDENT << "/*nb_add*/                  0," << endl;
    s << INDENT << "/*nb_subtract*/             0," << endl;
    s << INDENT << "/*nb_multiply*/             0," << endl;
    s << INDENT << "#ifndef IS_PY3K" << endl;
    s << INDENT << "/* nb_divide */             0," << endl;
    s << INDENT << "#endif" << endl;
    s << INDENT << "/*nb_remainder*/            0," << endl;
    s << INDENT << "/*nb_divmod*/               0," << endl;
    s << INDENT << "/*nb_power*/                0," << endl;
    s << INDENT << "/*nb_negative*/             0," << endl;
    s << INDENT << "/*nb_positive*/             0," << endl;
    s << INDENT << "/*nb_absolute*/             0," << endl;
    s << INDENT << "/*nb_nonzero*/              " << cpythonName << "__nonzero," << endl;
    s << INDENT << "/*nb_invert*/               (unaryfunc)" << cpythonName << "___invert__," << endl;
    s << INDENT << "/*nb_lshift*/               0," << endl;
    s << INDENT << "/*nb_rshift*/               0," << endl;
    s << INDENT << "/*nb_and*/                  (binaryfunc)" << cpythonName  << "___and__," << endl;
    s << INDENT << "/*nb_xor*/                  (binaryfunc)" << cpythonName  << "___xor__," << endl;
    s << INDENT << "/*nb_or*/                   (binaryfunc)" << cpythonName  << "___or__," << endl;
    s << INDENT << "#ifndef IS_PY3K" << endl;
    s << INDENT << "/* nb_coerce */             0," << endl;
    s << INDENT << "#endif" << endl;
    s << INDENT << "/*nb_int*/                  " << cpythonName << "_long," << endl;
    s << INDENT << "#ifdef IS_PY3K" << endl;
    s << INDENT << "/*nb_reserved*/             0," << endl;
    s << INDENT << "/*nb_float*/                0," << endl;
    s << INDENT << "#else" << endl;
    s << INDENT << "/*nb_long*/                 " << cpythonName << "_long," << endl;
    s << INDENT << "/*nb_float*/                0," << endl;
    s << INDENT << "/*nb_oct*/                  0," << endl;
    s << INDENT << "/*nb_hex*/                  0," << endl;
    s << INDENT << "#endif" << endl;
    s << INDENT << "/*nb_inplace_add*/          0," << endl;
    s << INDENT << "/*nb_inplace_subtract*/     0," << endl;
    s << INDENT << "/*nb_inplace_multiply*/     0," << endl;
    s << INDENT << "#ifndef IS_PY3K" << endl;
    s << INDENT << "/*nb_inplace_divide*/       0," << endl;
    s << INDENT << "#endif" << endl;
    s << INDENT << "/*nb_inplace_remainder*/    0," << endl;
    s << INDENT << "/*nb_inplace_power*/        0," << endl;
    s << INDENT << "/*nb_inplace_lshift*/       0," << endl;
    s << INDENT << "/*nb_inplace_rshift*/       0," << endl;
    s << INDENT << "/*nb_inplace_and*/          0," << endl;
    s << INDENT << "/*nb_inplace_xor*/          0," << endl;
    s << INDENT << "/*nb_inplace_or*/           0," << endl;
    s << INDENT << "/*nb_floor_divide*/         0," << endl;
    s << INDENT << "/*nb_true_divide*/          0," << endl;
    s << INDENT << "/*nb_inplace_floor_divide*/ 0," << endl;
    s << INDENT << "/*nb_inplace_true_divide*/  0," << endl;
    s << INDENT << "/*nb_index*/                0" << endl;
    s << "};" << endl << endl;
}

void CppGenerator::writeFlagsDefinition(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    FlagsTypeEntry* flagsEntry = cppEnum->typeEntry()->flags();
    if (!flagsEntry)
        return;
    QString cpythonName = cpythonFlagsName(flagsEntry);
    QString enumName = cpythonEnumName(cppEnum);

    s << "// forward declaration of new function" << endl;
    s << "static PyTypeObject " << cpythonName << "_Type = {" << endl;
    s << INDENT << "PyVarObject_HEAD_INIT(&PyType_Type, 0)" << endl;
    s << INDENT << "/*tp_name*/             \"" << flagsEntry->flagsName() << "\"," << endl;
    s << INDENT << "/*tp_basicsize*/        sizeof(PySideQFlagsObject)," << endl;
    s << INDENT << "/*tp_itemsize*/         0," << endl;
    s << INDENT << "/*tp_dealloc*/          0," << endl;
    s << INDENT << "/*tp_print*/            0," << endl;
    s << INDENT << "/*tp_getattr*/          0," << endl;
    s << INDENT << "/*tp_setattr*/          0," << endl;
    s << INDENT << "/*tp_compare*/          0," << endl;
    s << INDENT << "/*tp_repr*/             0," << endl;
    s << INDENT << "/*tp_as_number*/        &" << enumName << "_as_number," << endl;
    s << INDENT << "/*tp_as_sequence*/      0," << endl;
    s << INDENT << "/*tp_as_mapping*/       0," << endl;
    s << INDENT << "/*tp_hash*/             0," << endl;
    s << INDENT << "/*tp_call*/             0," << endl;
    s << INDENT << "/*tp_str*/              0," << endl;
    s << INDENT << "/*tp_getattro*/         0," << endl;
    s << INDENT << "/*tp_setattro*/         0," << endl;
    s << INDENT << "/*tp_as_buffer*/        0," << endl;
    s << INDENT << "/*tp_flags*/            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES," << endl;
    s << INDENT << "/*tp_doc*/              0," << endl;
    s << INDENT << "/*tp_traverse*/         0," << endl;
    s << INDENT << "/*tp_clear*/            0," << endl;
    s << INDENT << "/*tp_richcompare*/      &PySideQFlagsRichCompare," << endl;
    s << INDENT << "/*tp_weaklistoffset*/   0," << endl;
    s << INDENT << "/*tp_iter*/             0," << endl;
    s << INDENT << "/*tp_iternext*/         0," << endl;
    s << INDENT << "/*tp_methods*/          0," << endl;
    s << INDENT << "/*tp_members*/          0," << endl;
    s << INDENT << "/*tp_getset*/           0," << endl;
    s << INDENT << "/*tp_base*/             0," << endl;
    s << INDENT << "/*tp_dict*/             0," << endl;
    s << INDENT << "/*tp_descr_get*/        0," << endl;
    s << INDENT << "/*tp_descr_set*/        0," << endl;
    s << INDENT << "/*tp_dictoffset*/       0," << endl;
    s << INDENT << "/*tp_init*/             0," << endl;
    s << INDENT << "/*tp_alloc*/            0," << endl;
    s << INDENT << "/*tp_new*/              &PySideQFlagsNew," << endl;
    s << INDENT << "/*tp_free*/             0," << endl;
    s << INDENT << "/*tp_is_gc*/            0," << endl;
    s << INDENT << "/*tp_bases*/            0," << endl;
    s << INDENT << "/*tp_mro*/              0," << endl;
    s << INDENT << "/*tp_cache*/            0," << endl;
    s << INDENT << "/*tp_subclasses*/       0," << endl;
    s << INDENT << "/*tp_weaklist*/         0" << endl;
    s << "};" << endl << endl;
}

void CppGenerator::writeFlagsBinaryOperator(QTextStream& s, const AbstractMetaEnum* cppEnum,
                                            QString pyOpName, QString cppOpName)
{
    FlagsTypeEntry* flagsEntry = cppEnum->typeEntry()->flags();
    Q_ASSERT(flagsEntry);

    QString converter = "Shiboken::Converter< ::" + flagsEntry->originalName() + " >::";

    s << "PyObject* " << cpythonEnumName(cppEnum) << "___" << pyOpName << "__(PyObject* " PYTHON_SELF_VAR ", PyObject* " PYTHON_ARG ")" << endl;
    s << '{' << endl;

    s << INDENT << "return Shiboken::Converter< ::" << flagsEntry->originalName() << " >::toPython(" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "Shiboken::Converter< ::" << flagsEntry->originalName() << ">::toCpp(" PYTHON_SELF_VAR ")" << endl;
        s << INDENT << cppOpName << " Shiboken::Converter< ::";
        s << flagsEntry->originalName() << " >::toCpp(" PYTHON_ARG ")" << endl;
    }
    s << INDENT << ");" << endl;
    s << '}' << endl << endl;
}

void CppGenerator::writeFlagsUnaryOperator(QTextStream& s, const AbstractMetaEnum* cppEnum,
                                           QString pyOpName, QString cppOpName, bool boolResult)
{
    FlagsTypeEntry* flagsEntry = cppEnum->typeEntry()->flags();
    Q_ASSERT(flagsEntry);

    QString converter = "Shiboken::Converter< ::" + flagsEntry->originalName() + " >::";

    s << "PyObject* " << cpythonEnumName(cppEnum) << "___" << pyOpName << "__(PyObject* " PYTHON_SELF_VAR ", PyObject* " PYTHON_ARG ")" << endl;
    s << '{' << endl;
    s << INDENT << "return Shiboken::Converter< " << (boolResult ? "bool" : flagsEntry->originalName());
    s << " >::toPython(" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << cppOpName << converter << "toCpp(" PYTHON_SELF_VAR ")" << endl;
    }
    s << INDENT << ");" << endl;
    s << '}' << endl << endl;
}

void CppGenerator::writeClassRegister(QTextStream& s, const AbstractMetaClass* metaClass)
{
    const ComplexTypeEntry* classTypeEntry = metaClass->typeEntry();

    const AbstractMetaClass* enc = metaClass->enclosingClass();
    bool hasEnclosingClass = enc && enc->typeEntry()->codeGeneration() != TypeEntry::GenerateForSubclass;
    QString enclosingObjectVariable = hasEnclosingClass ? "enclosingClass" : "module";

    QString pyTypeName = cpythonTypeName(metaClass);
    s << "void init_" << metaClass->qualifiedCppName().replace("::", "_");
    s << "(PyObject* " << enclosingObjectVariable << ")" << endl;
    s << '{' << endl;

    if (supportsNumberProtocol(metaClass)) {
        s << INDENT << "// type has number operators" << endl;
        writeTypeAsNumberDefinition(s, metaClass);
        s << INDENT << pyTypeName << ".super.ht_type.tp_as_number = &" << pyTypeName << ".super.as_number;" << endl;
        s << endl;
    }

    if (supportsSequenceProtocol(metaClass)) {
        s << INDENT << "// type supports sequence protocol" << endl;
        writeTypeAsSequenceDefinition(s, metaClass);
        s << INDENT << pyTypeName << ".super.ht_type.tp_as_sequence = &" << pyTypeName << ".super.as_sequence;" << endl;
        s << endl;
    }

    if (supportsMappingProtocol(metaClass)) {
        s << INDENT << "// type supports mapping protocol" << endl;
        writeTypeAsMappingDefinition(s, metaClass);
        s << INDENT << pyTypeName << ".super.ht_type.tp_as_mapping = &" << pyTypeName << ".super.as_mapping;" << endl;
        s << endl;
    }

    s << INDENT << cpythonTypeNameExt(classTypeEntry);
    s << " = reinterpret_cast<PyTypeObject*>(&" << pyTypeName << ");" << endl;
    s << endl;

    // Multiple inheritance
    QString pyTypeBasesVariable = QString("%1_bases").arg(pyTypeName);
    const AbstractMetaClassList baseClasses = getBaseClasses(metaClass);
    if (metaClass->baseClassNames().size() > 1) {
        s << INDENT << "PyObject* " << pyTypeBasesVariable << " = PyTuple_Pack(" << baseClasses.size() << ',' << endl;
        QStringList bases;
        foreach (const AbstractMetaClass* base, baseClasses)
            bases << "(PyObject*)" + cpythonTypeNameExt(base->typeEntry());
        Indentation indent(INDENT);
        QString separator;
        QTextStream sep(&separator);
        sep << "," << endl << INDENT;
        s << INDENT << bases.join(separator) << ");" << endl << endl;
    }

    // Create type and insert it in the module or enclosing class.
    s << INDENT << "if (!Shiboken::ObjectType::introduceWrapperType(" << enclosingObjectVariable;
    s << ", \"" << metaClass->name() << "\", \"";
    // Original name
    s << metaClass->qualifiedCppName() << (ShibokenGenerator::isObjectType(classTypeEntry) ?  "*" : "");
    s << "\"," << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "&" << pyTypeName;

        // Set destructor function
        if (!metaClass->isNamespace() && !metaClass->hasPrivateDestructor()) {
            QString dtorClassName = metaClass->qualifiedCppName();
            if ((avoidProtectedHack() && metaClass->hasProtectedDestructor()) || classTypeEntry->isValue())
                dtorClassName = wrapperName(metaClass);
            s << ", &Shiboken::callCppDestructor< ::" << dtorClassName << " >";
        } else if (metaClass->baseClass() || hasEnclosingClass) {
            s << ", 0";
        }

        // Base type
        if (metaClass->baseClass()) {
            s << ", (SbkObjectType*)" << cpythonTypeNameExt(metaClass->baseClass()->typeEntry());
            // The other base types
            if (metaClass->baseClassNames().size() > 1)
                s << ", " << pyTypeBasesVariable;
            else if (hasEnclosingClass)
                s << ", 0";
        } else if (hasEnclosingClass) {
            s << ", 0, 0";
        }
        if (hasEnclosingClass)
            s << ", true";
        s << ")) {" << endl;
        s << INDENT << "return;" << endl;
    }
    s << INDENT << '}' << endl << endl;

    // class inject-code target/beginning
    if (!classTypeEntry->codeSnips().isEmpty()) {
        writeCodeSnips(s, classTypeEntry->codeSnips(), CodeSnip::Beginning, TypeSystem::TargetLangCode, metaClass);
        s << endl;
    }

    // Fill multiple inheritance data, if needed.
    const AbstractMetaClass* miClass = getMultipleInheritingClass(metaClass);
    if (miClass) {
        s << INDENT << "MultipleInheritanceInitFunction func = ";
        if (miClass == metaClass) {
            s << multipleInheritanceInitializerFunctionName(miClass) << ";" << endl;
        } else {
            s << "Shiboken::ObjectType::getMultipleIheritanceFunction(reinterpret_cast<SbkObjectType*>(";
            s << cpythonTypeNameExt(miClass->typeEntry()) << "));" << endl;
        }
        s << INDENT << "Shiboken::ObjectType::setMultipleIheritanceFunction(&";
        s << cpythonTypeName(metaClass) << ", func);" << endl;
        s << INDENT << "Shiboken::ObjectType::setCastFunction(&" << cpythonTypeName(metaClass);
        s << ", &" << cpythonSpecialCastFunctionName(metaClass) << ");" << endl;
    }

    // Set typediscovery struct or fill the struct of another one
    if (metaClass->isPolymorphic() && metaClass->baseClass()) {
        s << INDENT << "Shiboken::ObjectType::setTypeDiscoveryFunctionV2(&" << cpythonTypeName(metaClass);
        s << ", &" << cpythonBaseName(metaClass) << "_typeDiscovery);" << endl << endl;
    }

    AbstractMetaEnumList classEnums = metaClass->enums();
    foreach (AbstractMetaClass* innerClass, metaClass->innerClasses())
        lookForEnumsInClassesNotToBeGenerated(classEnums, innerClass);

    ErrorCode errorCode("");
    writeEnumsInitialization(s, classEnums);

    if (metaClass->hasSignals())
        writeSignalInitialization(s, metaClass);

    // Write static fields
    foreach (const AbstractMetaField* field, metaClass->fields()) {
        if (!field->isStatic())
            continue;
        s << INDENT << "PyDict_SetItemString(" + cpythonTypeName(metaClass) + ".super.ht_type.tp_dict, \"";
        s << field->name() << "\", ";
        writeToPythonConversion(s, field->type(), metaClass, metaClass->qualifiedCppName() + "::" + field->name());
        s << ");" << endl;
    }
    s << endl;

    // class inject-code target/end
    if (!classTypeEntry->codeSnips().isEmpty()) {
        s << endl;
        writeCodeSnips(s, classTypeEntry->codeSnips(), CodeSnip::End, TypeSystem::TargetLangCode, metaClass);
    }

    if (!metaClass->isNamespace())
        writeRegisterType(s, metaClass);

    if (usePySideExtensions()) {
        if (avoidProtectedHack() && shouldGenerateCppWrapper(metaClass))
            s << INDENT << wrapperName(metaClass) << "::pysideInitQtMetaTypes();\n";
        else
            writeInitQtMetaTypeFunctionBody(s, metaClass);
    }

    if (usePySideExtensions() && metaClass->isQObject()) {
        s << INDENT << "Shiboken::ObjectType::setSubTypeInitHook(&" << pyTypeName << ", &PySide::initQObjectSubType);" << endl;
        s << INDENT << "PySide::initDynamicMetaObject(&" << pyTypeName << ", &::" << metaClass->qualifiedCppName()
          << "::staticMetaObject, sizeof(::" << metaClass->qualifiedCppName() << "));" << endl;
    }

    s << '}' << endl;
}

void CppGenerator::writeInitQtMetaTypeFunctionBody(QTextStream& s, const AbstractMetaClass* metaClass) const
{
    // Gets all class name variants used on different possible scopes
    QStringList nameVariants;
    nameVariants << metaClass->name();
    const AbstractMetaClass* enclosingClass = metaClass->enclosingClass();
    while (enclosingClass) {
        if (enclosingClass->typeEntry()->generateCode())
            nameVariants << (enclosingClass->name() + "::" + nameVariants.last());
        enclosingClass = enclosingClass->enclosingClass();
    }

    const QString className = metaClass->qualifiedCppName();
    if (!metaClass->isNamespace() && !metaClass->isAbstract())  {
        // Qt metatypes are registered only on their first use, so we do this now.
        bool canBeValue = false;
        if (!isObjectType(metaClass)) {
            // check if there's a empty ctor
            foreach (AbstractMetaFunction* func, metaClass->functions()) {
                if (func->isConstructor() && !func->arguments().count()) {
                    canBeValue = true;
                    break;
                }
            }
        }

        if (canBeValue) {
            foreach (QString name, nameVariants)
                s << INDENT << "qRegisterMetaType< ::" << className << " >(\"" << name << "\");" << endl;
        }
    }

    foreach (AbstractMetaEnum* metaEnum, metaClass->enums()) {
        if (!metaEnum->isPrivate() && !metaEnum->isAnonymous()) {
            foreach (QString name, nameVariants)
                s << INDENT << "qRegisterMetaType< ::" << metaEnum->typeEntry()->qualifiedCppName() << " >(\"" << name << "::" << metaEnum->name() << "\");" << endl;

            if (metaEnum->typeEntry()->flags()) {
                QString n = metaEnum->typeEntry()->flags()->originalName();
                s << INDENT << "qRegisterMetaType< ::" << n << " >(\"" << n << "\");" << endl;
            }
        }
    }
}

void CppGenerator::writeTypeDiscoveryFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString polymorphicExpr = metaClass->typeEntry()->polymorphicIdValue();

    s << "static void* " << cpythonBaseName(metaClass) << "_typeDiscovery(void* cptr, SbkObjectType* instanceType)\n{" << endl;

    if (!polymorphicExpr.isEmpty()) {
        polymorphicExpr = polymorphicExpr.replace("%1", " reinterpret_cast< ::" + metaClass->qualifiedCppName() + "*>(cptr)");
        s << INDENT << " if (" << polymorphicExpr << ")" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "return cptr;" << endl;
        }
    } else if (metaClass->isPolymorphic()) {
        AbstractMetaClassList ancestors = getAllAncestors(metaClass);
        foreach (AbstractMetaClass* ancestor, ancestors) {
            if (ancestor->baseClass())
                continue;
            if (ancestor->isPolymorphic()) {
                s << INDENT << "if (instanceType == reinterpret_cast<SbkObjectType*>(Shiboken::SbkType< ::"
                            << ancestor->qualifiedCppName() << " >()))" << endl;
                Indentation indent(INDENT);
                s << INDENT << "return dynamic_cast< ::" << metaClass->qualifiedCppName()
                            << "*>(reinterpret_cast< ::"<< ancestor->qualifiedCppName() << "*>(cptr));" << endl;
            } else {
                ReportHandler::warning(metaClass->qualifiedCppName() + " inherits from a non polymorphic type ("
                                       + ancestor->qualifiedCppName() + "), type discovery based on RTTI is "
                                       "impossible, write a polymorphic-id-expression for this type.");
            }

        }
    }
    s << INDENT << "return 0;" << endl;
    s << "}\n\n";
}

void CppGenerator::writeSetattroFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    s << "static int " << cpythonSetattroFunctionName(metaClass) << "(PyObject* " PYTHON_SELF_VAR ", PyObject* name, PyObject* value)" << endl;
    s << '{' << endl;
    if (usePySideExtensions()) {
        s << INDENT << "Shiboken::AutoDecRef pp(reinterpret_cast<PyObject*>(PySide::Property::getObject(" PYTHON_SELF_VAR ", name)));" << endl;
        s << INDENT << "if (!pp.isNull())" << endl;
        Indentation indent(INDENT);
        s << INDENT << "return PySide::Property::setValue(reinterpret_cast<PySideProperty*>(pp.object()), " PYTHON_SELF_VAR ", value);" << endl;
    }
    s << INDENT << "return PyObject_GenericSetAttr(" PYTHON_SELF_VAR ", name, value);" << endl;
    s << '}' << endl;
}

void CppGenerator::writeGetattroFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    s << "static PyObject* " << cpythonGetattroFunctionName(metaClass) << "(PyObject* " PYTHON_SELF_VAR ", PyObject* name)" << endl;
    s << '{' << endl;

    QString getattrFunc;
    if (usePySideExtensions() && metaClass->isQObject())
        getattrFunc = "PySide::getMetaDataFromQObject(Shiboken::Converter< ::QObject*>::toCpp(" PYTHON_SELF_VAR "), " PYTHON_SELF_VAR ", name)";
    else
        getattrFunc = "PyObject_GenericGetAttr(" PYTHON_SELF_VAR ", name)";

    if (classNeedsGetattroFunction(metaClass)) {
        s << INDENT << "if (" PYTHON_SELF_VAR ") {" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "// Search the method in the instance dict" << endl;
            s << INDENT << "if (reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR ")->ob_dict) {" << endl;
            {
                Indentation indent(INDENT);
                s << INDENT << "PyObject* meth = PyDict_GetItem(reinterpret_cast<SbkObject*>(" PYTHON_SELF_VAR ")->ob_dict, name);" << endl;
                s << INDENT << "if (meth) {" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "Py_INCREF(meth);" << endl;
                    s << INDENT << "return meth;" << endl;
                }
                s << INDENT << '}' << endl;
            }
            s << INDENT << '}' << endl;
            s << INDENT << "// Search the method in the type dict" << endl;
            s << INDENT << "if (Shiboken::Object::isUserType(" PYTHON_SELF_VAR ")) {" << endl;
            {
                Indentation indent(INDENT);
                s << INDENT << "PyObject* meth = PyDict_GetItem(" PYTHON_SELF_VAR "->ob_type->tp_dict, name);" << endl;
                s << INDENT << "if (meth)" << endl;
                {
                    Indentation indent(INDENT);
                    s << INDENT << "return PyFunction_Check(meth) ? SBK_PyMethod_New(meth, " PYTHON_SELF_VAR ") : " << getattrFunc << ';' << endl;
                }
            }
            s << INDENT << '}' << endl;

            foreach (const AbstractMetaFunction* func, getMethodsWithBothStaticAndNonStaticMethods(metaClass)) {
                s << INDENT << "if (Shiboken::String::compare(name, \"" << func->name() << "\") == 0)" << endl;
                Indentation indent(INDENT);
                s << INDENT << "return PyCFunction_NewEx(&" << cpythonMethodDefinitionName(func) << ", " PYTHON_SELF_VAR ", 0);" << endl;
            }
        }
        s << INDENT << '}' << endl;
    }
    s << INDENT << "return " << getattrFunc << ';' << endl;
    s << '}' << endl;
}

void CppGenerator::finishGeneration()
{
    //Generate CPython wrapper file
    QString classInitDecl;
    QTextStream s_classInitDecl(&classInitDecl);
    QString classPythonDefines;
    QTextStream s_classPythonDefines(&classPythonDefines);

    QSet<Include> includes;
    QString globalFunctionImpl;
    QTextStream s_globalFunctionImpl(&globalFunctionImpl);
    QString globalFunctionDecl;
    QTextStream s_globalFunctionDef(&globalFunctionDecl);

    Indentation indent(INDENT);

    foreach (AbstractMetaFunctionList globalOverloads, getFunctionGroups().values()) {
        AbstractMetaFunctionList overloads;
        foreach (AbstractMetaFunction* func, globalOverloads) {
            if (!func->isModifiedRemoved()) {
                overloads.append(func);
                if (func->typeEntry())
                    includes << func->typeEntry()->include();
            }
        }

        if (overloads.isEmpty())
            continue;

        writeMethodWrapper(s_globalFunctionImpl, overloads);
        writeMethodDefinition(s_globalFunctionDef, overloads);
    }

    //this is a temporary solution before new type revison implementation
    //We need move QMetaObject register before QObject
    AbstractMetaClassList lst = classes();
    AbstractMetaClass* klassQObject = lst.findClass("QObject");
    AbstractMetaClass* klassQMetaObject = lst.findClass("QMetaObject");
    if (klassQObject && klassQMetaObject) {
        lst.removeAll(klassQMetaObject);
        int indexOf = lst.indexOf(klassQObject);
        lst.insert(indexOf, klassQMetaObject);
    }

    foreach (const AbstractMetaClass* cls, lst) {
        if (!shouldGenerate(cls))
            continue;

        s_classInitDecl << "void init_" << cls->qualifiedCppName().replace("::", "_") << "(PyObject* module);" << endl;

        QString defineStr = "init_" + cls->qualifiedCppName().replace("::", "_");

        if (cls->enclosingClass() && (cls->enclosingClass()->typeEntry()->codeGeneration() != TypeEntry::GenerateForSubclass))
            defineStr += "(" + cpythonTypeNameExt(cls->enclosingClass()->typeEntry()) +"->tp_dict);";
        else
            defineStr += "(module);";
        s_classPythonDefines << INDENT << defineStr << endl;
    }

    QString moduleFileName(outputDirectory() + "/" + subDirectoryForPackage(packageName()));
    moduleFileName += "/" + moduleName().toLower() + "_module_wrapper.cpp";

    QFile file(moduleFileName);
    verifyDirectoryFor(file);
    if (!file.open(QFile::WriteOnly)) {
        ReportHandler::warning("Error writing file: " + moduleFileName);
        return;
    }

    QTextStream s(&file);

    // write license comment
    s << licenseComment() << endl;

    s << "#include <sbkpython.h>" << endl;
    s << "#include <shiboken.h>" << endl;
    s << "#include <algorithm>" << endl;
    if (usePySideExtensions())
        s << "#include <pyside.h>" << endl;

    s << "#include \"" << getModuleHeaderFileName() << '"' << endl << endl;
    foreach (const Include& include, includes)
        s << include;
    s << endl;

    // Global enums
    AbstractMetaEnumList globalEnums = this->globalEnums();
    foreach (const AbstractMetaClass* metaClass, classes()) {
        const AbstractMetaClass* encClass = metaClass->enclosingClass();
        if (encClass && encClass->typeEntry()->codeGeneration() != TypeEntry::GenerateForSubclass)
            continue;
        lookForEnumsInClassesNotToBeGenerated(globalEnums, metaClass);
    }

    TypeDatabase* typeDb = TypeDatabase::instance();
    TypeSystemTypeEntry* moduleEntry = reinterpret_cast<TypeSystemTypeEntry*>(typeDb->findType(packageName()));

    //Extra includes
    s << endl << "// Extra includes" << endl;
    QList<Include> extraIncludes;
    if (moduleEntry)
        extraIncludes = moduleEntry->extraIncludes();
    foreach (AbstractMetaEnum* cppEnum, globalEnums)
        extraIncludes.append(cppEnum->typeEntry()->extraIncludes());
    qSort(extraIncludes.begin(), extraIncludes.end());
    foreach (const Include& inc, extraIncludes)
        s << inc;
    s << endl;

    CodeSnipList snips;
    if (moduleEntry)
        snips = moduleEntry->codeSnips();

    // module inject-code native/beginning
    if (!snips.isEmpty()) {
        writeCodeSnips(s, snips, CodeSnip::Beginning, TypeSystem::NativeCode);
        s << endl;
    }

    // cleanup staticMetaObject attribute
    if (usePySideExtensions()) {
        s << "void cleanTypesAttributes(void) {" << endl;
        s << INDENT << "for (int i = 0, imax = SBK_" << moduleName() << "_IDX_COUNT; i < imax; i++) {" << endl;
        {
            Indentation indentation(INDENT);
            s << INDENT << "PyObject *pyType = reinterpret_cast<PyObject*>(" << cppApiVariableName() << "[i]);" << endl;
            s << INDENT << "if (pyType && PyObject_HasAttrString(pyType, \"staticMetaObject\"))"<< endl;
            {
                Indentation indentation(INDENT);
                s << INDENT << "PyObject_SetAttrString(pyType, \"staticMetaObject\", Py_None);" << endl;
            }
        }
        s << INDENT << "}" << endl;
        s << "}" << endl;
    }

    s << "// Global functions ";
    s << "------------------------------------------------------------" << endl;
    s << globalFunctionImpl << endl;

    s << "static PyMethodDef " << moduleName() << "_methods[] = {" << endl;
    s << globalFunctionDecl;
    s << INDENT << "{0} // Sentinel" << endl << "};" << endl << endl;

    s << "// Classes initialization functions ";
    s << "------------------------------------------------------------" << endl;
    s << classInitDecl << endl;

    if (!globalEnums.isEmpty()) {
        QString converterImpl;
        QTextStream convImpl(&converterImpl);

        s << "// Enum definitions ";
        s << "------------------------------------------------------------" << endl;
        foreach (const AbstractMetaEnum* cppEnum, globalEnums) {
            if (cppEnum->isAnonymous() || cppEnum->isPrivate())
                continue;
            s << endl;
        }

        if (!converterImpl.isEmpty()) {
            s << "// Enum converters ";
            s << "------------------------------------------------------------" << endl;
            s << "namespace Shiboken" << endl << '{' << endl;
            s << converterImpl << endl;
            s << "} // namespace Shiboken" << endl << endl;
        }
    }

    s << "// Current module's type array." << endl;
    s << "PyTypeObject** " << cppApiVariableName() << ';' << endl;
    s << "// Required modules' type arrays." << endl;
    foreach (const QString& requiredModule, typeDb->requiredTargetImports())
        s << "PyTypeObject** " << cppApiVariableName(requiredModule) << ';' << endl;
    s << endl;

    s << "// Module initialization ";
    s << "------------------------------------------------------------" << endl;
    ExtendedConverterData extendedConverters = getExtendedConverters();
    if (!extendedConverters.isEmpty())
        s << "// Extended Converters" << endl;
    foreach (const TypeEntry* externalType, extendedConverters.keys()) {
        writeExtendedIsConvertibleFunction(s, externalType, extendedConverters[externalType]);
        writeExtendedToCppFunction(s, externalType, extendedConverters[externalType]);
        s << endl;
    }
    s << endl;


    s << "#if defined _WIN32 || defined __CYGWIN__" << endl;
    s << "    #define SBK_EXPORT_MODULE __declspec(dllexport)" << endl;
    s << "#elif __GNUC__ >= 4" << endl;
    s << "    #define SBK_EXPORT_MODULE __attribute__ ((visibility(\"default\")))" << endl;
    s << "#else" << endl;
    s << "    #define SBK_EXPORT_MODULE" << endl;
    s << "#endif" << endl << endl;

    s << "#ifdef IS_PY3K" << endl;

    if (usePySideExtensions()) {
        s << "static int " << moduleName() << "_moduleTraverse(PyObject*, visitproc, void *)" << endl
          << "{" << endl
          << INDENT << "PySide::runCleanupFunctions();" << endl
          << "}" << endl << endl;
    }

    s << "static struct PyModuleDef moduledef = {" << endl;
    s << "    /* m_base     */ PyModuleDef_HEAD_INIT," << endl;
    s << "    /* m_name     */ \"" << moduleName() << "\"," << endl;
    s << "    /* m_doc      */ 0," << endl;
    s << "    /* m_size     */ -1," << endl;
    s << "    /* m_methods  */ " << moduleName() << "_methods," << endl;
    s << "    /* m_reload   */ 0," << endl;

    if (usePySideExtensions())
        s << "    /* m_traverse */ " << moduleName() << "_moduleTraverse," << endl;
    else
        s << "    /* m_traverse */ 0," << endl;

    s << "    /* m_clear    */ 0," << endl;
    s << "    /* m_free     */ 0" << endl;
    s << "};" << endl << endl;
    s << "#endif" << endl;
    s << "SBK_MODULE_INIT_FUNCTION_BEGIN(" << moduleName() << ")" << endl;

    ErrorCode errorCode("SBK_MODULE_INIT_ERROR");
    // module inject-code target/beginning
    if (!snips.isEmpty()) {
        writeCodeSnips(s, snips, CodeSnip::Beginning, TypeSystem::TargetLangCode);
        s << endl;
    }

    foreach (const QString& requiredModule, typeDb->requiredTargetImports()) {
        s << INDENT << "{" << endl;
        {
            Indentation indentation(INDENT);
            s << INDENT << "Shiboken::AutoDecRef requiredModule(Shiboken::Module::import(\"" << requiredModule << "\"));" << endl;
            s << INDENT << "if (requiredModule.isNull())" << endl;
            {
                Indentation indentation(INDENT);
                s << INDENT << "SBK_MODULE_INIT_ERROR;" << endl;
            }
            s << INDENT << cppApiVariableName(requiredModule) << " = Shiboken::Module::getTypes(requiredModule);" << endl;
        }
        s << INDENT << "}" << endl << endl;
    }

    s << INDENT << "// Create an array of wrapper types for the current module." << endl;
    s << INDENT << "static PyTypeObject* cppApi[" << "SBK_" << moduleName() << "_IDX_COUNT" << "];" << endl;
    s << INDENT << cppApiVariableName() << " = cppApi;" << endl << endl;

    s << "#ifdef IS_PY3K" << endl;
    s << INDENT << "PyObject* module = Shiboken::Module::create(\""  << moduleName() << "\", &moduledef);" << endl;
    s << "#else" << endl;
    s << INDENT << "PyObject* module = Shiboken::Module::create(\""  << moduleName() << "\", ";
    s << moduleName() << "_methods);" << endl;
    s << "#endif" << endl << endl;

    s << INDENT << "// Initialize classes in the type system" << endl;
    s << classPythonDefines;

    if (!extendedConverters.isEmpty()) {
        s << INDENT << "// Initialize extended Converters" << endl;
        s << INDENT << "SbkObjectType* shiboType;" << endl << endl;
    }
    foreach (const TypeEntry* externalType, extendedConverters.keys()) {
        writeExtendedConverterInitialization(s, externalType, extendedConverters[externalType]);
        s << endl;
    }
    s << endl;

    writeEnumsInitialization(s, globalEnums);

    // Register primitive types on TypeResolver
    s << INDENT << "// Register primitive types on TypeResolver" << endl;
    foreach(const PrimitiveTypeEntry* pte, primitiveTypes()) {
        if (pte->generateCode())
            s << INDENT << typeResolverString(pte->name()) << ';' << endl;
    }
    // Register type resolver for all containers found in signals.
    QSet<QByteArray> typeResolvers;
    foreach (AbstractMetaClass* metaClass, classes()) {
        if (!metaClass->isQObject() || !metaClass->typeEntry()->generateCode())
            continue;
        foreach (AbstractMetaFunction* func, metaClass->functions()) {
            if (func->isSignal()) {
                foreach (AbstractMetaArgument* arg, func->arguments()) {
                    if (arg->type()->isContainer()) {
                        QString value = translateType(arg->type(), metaClass, ExcludeConst | ExcludeReference);
                        if (value.startsWith("::"))
                            value.remove(0, 2);
                        typeResolvers << SBK_NORMALIZED_TYPE(value.toAscii().constData());
                    }
                }
            }
        }
    }
    foreach (QByteArray type, typeResolvers)
        s << INDENT << typeResolverString(type) << ';' << endl;

    s << endl << INDENT << "Shiboken::Module::registerTypes(module, " << cppApiVariableName() << ");" << endl;

    s << endl << INDENT << "if (PyErr_Occurred()) {" << endl;
    {
        Indentation indentation(INDENT);
        s << INDENT << "PyErr_Print();" << endl;
        s << INDENT << "Py_FatalError(\"can't initialize module " << moduleName() << "\");" << endl;
    }
    s << INDENT << '}' << endl;

    // module inject-code target/end
    if (!snips.isEmpty()) {
        writeCodeSnips(s, snips, CodeSnip::End, TypeSystem::TargetLangCode);
        s << endl;
    }

    // module inject-code native/end
    if (!snips.isEmpty()) {
        writeCodeSnips(s, snips, CodeSnip::End, TypeSystem::NativeCode);
        s << endl;
    }

    if (usePySideExtensions()) {
        foreach (AbstractMetaEnum* metaEnum, globalEnums)
            if (!metaEnum->isAnonymous()) {
                s << INDENT << "qRegisterMetaType< ::" << metaEnum->typeEntry()->qualifiedCppName() << " >(\"" << metaEnum->name() << "\");" << endl;
            }

        // cleanup staticMetaObject attribute
        s << INDENT << "PySide::registerCleanupFunction(cleanTypesAttributes);" << endl;
    }

    s << "SBK_MODULE_INIT_FUNCTION_END" << endl;
}

static ArgumentOwner getArgumentOwner(const AbstractMetaFunction* func, int argIndex)
{
    ArgumentOwner argOwner = func->argumentOwner(func->ownerClass(), argIndex);
    if (argOwner.index == ArgumentOwner::InvalidIndex)
        argOwner = func->argumentOwner(func->declaringClass(), argIndex);
    return argOwner;
}

bool CppGenerator::writeParentChildManagement(QTextStream& s, const AbstractMetaFunction* func, int argIndex, bool useHeuristicPolicy)
{
    const int numArgs = func->arguments().count();
    bool ctorHeuristicEnabled = func->isConstructor() && useCtorHeuristic() && useHeuristicPolicy;

    bool usePyArgs = pythonFunctionWrapperUsesListOfArguments(OverloadData(getFunctionGroups(func->implementingClass())[func->name()], this));

    ArgumentOwner argOwner = getArgumentOwner(func, argIndex);
    ArgumentOwner::Action action = argOwner.action;
    int parentIndex = argOwner.index;
    int childIndex = argIndex;
    if (ctorHeuristicEnabled && argIndex > 0 && numArgs) {
        AbstractMetaArgument* arg = func->arguments().at(argIndex-1);
        if (arg->name() == "parent" && isObjectType(arg->type())) {
            action = ArgumentOwner::Add;
            parentIndex = argIndex;
            childIndex = -1;
        }
    }

    QString parentVariable;
    QString childVariable;
    if (action != ArgumentOwner::Invalid) {
        if (!usePyArgs && argIndex > 1)
            ReportHandler::warning("Argument index for parent tag out of bounds: "+func->signature());

        if (action == ArgumentOwner::Remove) {
            parentVariable = "Py_None";
        } else {
            if (parentIndex == 0)
                parentVariable = PYTHON_RETURN_VAR;
            else if (parentIndex == -1)
                parentVariable = PYTHON_SELF_VAR;
            else
                parentVariable = usePyArgs ? QString(PYTHON_ARGS "[%1]").arg(QString::number(parentIndex-1)) : PYTHON_ARG;
        }

        if (childIndex == 0)
            childVariable = PYTHON_RETURN_VAR;
        else if (childIndex == -1)
            childVariable = PYTHON_SELF_VAR;
        else
            childVariable = usePyArgs ? QString(PYTHON_ARGS "[%1]").arg(QString::number(childIndex-1)) : PYTHON_ARG;

        s << INDENT << "Shiboken::Object::setParent(" << parentVariable << ", " << childVariable << ");\n";
        return true;
    }

    return false;
}

void CppGenerator::writeParentChildManagement(QTextStream& s, const AbstractMetaFunction* func, bool useHeuristicForReturn)
{
    const int numArgs = func->arguments().count();

    // -1    = return value
    //  0    = self
    //  1..n = func. args.
    for (int i = -1; i <= numArgs; ++i)
        writeParentChildManagement(s, func, i, useHeuristicForReturn);

    if (useHeuristicForReturn)
        writeReturnValueHeuristics(s, func);
}

void CppGenerator::writeReturnValueHeuristics(QTextStream& s, const AbstractMetaFunction* func, const QString& self)
{
    AbstractMetaType *type = func->type();
    if (!useReturnValueHeuristic()
        || !func->ownerClass()
        || !type
        || func->isStatic()
        || !func->typeReplaced(0).isEmpty()) {
        return;
    }

    ArgumentOwner argOwner = getArgumentOwner(func, ArgumentOwner::ReturnIndex);
    if (argOwner.action == ArgumentOwner::Invalid || argOwner.index != ArgumentOwner::ThisIndex) {
        if (isPointerToWrapperType(type))
            s << INDENT << "Shiboken::Object::setParent(" << self << ", " PYTHON_RETURN_VAR ");" << endl;
    }
}

void CppGenerator::writeHashFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    s << "static long " << cpythonBaseName(metaClass) << "_HashFunc(PyObject* self) {" << endl;
    writeCppSelfDefinition(s, metaClass);
    s << INDENT << "return " << metaClass->typeEntry()->hashFunction() << '(';
    s << (isObjectType(metaClass) ? "" : "*") << CPP_SELF_VAR << ");" << endl;
    s << '}' << endl << endl;
}

void CppGenerator::writeStdListWrapperMethods(QTextStream& s, const AbstractMetaClass* metaClass)
{
    ErrorCode errorCode(0);

    // __len__
    s << "Py_ssize_t " << cpythonBaseName(metaClass->typeEntry()) << "__len__(PyObject* " PYTHON_SELF_VAR ")" << endl;
    s << '{' << endl;
    writeCppSelfDefinition(s, metaClass);
    s << INDENT << "return " CPP_SELF_VAR "->size();" << endl;
    s << '}' << endl;

    // __getitem__
    s << "PyObject* " << cpythonBaseName(metaClass->typeEntry()) << "__getitem__(PyObject* " PYTHON_SELF_VAR ", Py_ssize_t _i)" << endl;
    s << '{' << endl;
    writeCppSelfDefinition(s, metaClass);
    writeIndexError(s, "index out of bounds");
    s << INDENT << metaClass->qualifiedCppName() << "::iterator _item = " CPP_SELF_VAR "->begin();" << endl;
    s << INDENT << "for(Py_ssize_t pos=0; pos < _i; pos++) _item++;" << endl;
    s << INDENT << "return Shiboken::Converter< ::" << metaClass->qualifiedCppName() << "::value_type>::toPython(*_item);" << endl;
    s << '}' << endl;

    // __setitem__
    ErrorCode errorCode2(-1);
    s << "int " << cpythonBaseName(metaClass->typeEntry()) << "__setitem__(PyObject* " PYTHON_SELF_VAR ", Py_ssize_t _i, PyObject* _value)" << endl;
    s << '{' << endl;
    writeCppSelfDefinition(s, metaClass);
    writeIndexError(s, "list assignment index out of range");
    s << INDENT << metaClass->qualifiedCppName() << "::iterator _item = " CPP_SELF_VAR "->begin();" << endl;
    s << INDENT << "for(Py_ssize_t pos=0; pos < _i; pos++) _item++;" << endl;
    s << INDENT << metaClass->qualifiedCppName() << "::value_type cppValue = Shiboken::Converter< ::" <<  metaClass->qualifiedCppName() << "::value_type>::toCpp(_value);" << endl;
    s << INDENT << "*_item = cppValue;" << endl;
    s << INDENT << "return 0;" << endl;
    s << '}' << endl;
}
void CppGenerator::writeIndexError(QTextStream& s, const QString& errorMsg)
{
    s << INDENT << "if (_i < 0 || _i >= (Py_ssize_t) " CPP_SELF_VAR "->size()) {" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "PyErr_SetString(PyExc_IndexError, \"" << errorMsg << "\");" << endl;
        s << INDENT << "return " << m_currentErrorCode << ';' << endl;
    }
    s << INDENT << '}' << endl;
}

QString CppGenerator::writeReprFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    QString funcName = cpythonBaseName(metaClass) + "__repr__";
    s << "extern \"C\"" << endl;
    s << '{' << endl;
    s << "static PyObject* " << funcName << "(PyObject* self)" << endl;
    s << '{' << endl;
    writeCppSelfDefinition(s, metaClass);
    s << INDENT << "QBuffer buffer;" << endl;
    s << INDENT << "buffer.open(QBuffer::ReadWrite);" << endl;
    s << INDENT << "QDebug dbg(&buffer);" << endl;
    s << INDENT << "dbg << " << (metaClass->typeEntry()->isValue() ? "*" : "") << CPP_SELF_VAR ";" << endl;
    s << INDENT << "buffer.close();" << endl;
    s << INDENT << "QByteArray str = buffer.data();" << endl;
    s << INDENT << "int idx = str.indexOf('(');" << endl;
    s << INDENT << "if (idx >= 0)" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "str.replace(0, idx, Py_TYPE(self)->tp_name);" << endl;
    }
    s << INDENT << "PyObject* mod = PyDict_GetItemString(Py_TYPE(self)->tp_dict, \"__module__\");" << endl;
    s << INDENT << "if (mod)" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "return Shiboken::String::fromFormat(\"<%s.%s at %p>\", Shiboken::String::toCString(mod), str.constData(), self);" << endl;
    }
    s << INDENT << "else" << endl;
    {
        Indentation indent(INDENT);
        s << INDENT << "return Shiboken::String::fromFormat(\"<%s at %p>\", str.constData(), self);" << endl;
    }
    s << '}' << endl;
    s << "} // extern C" << endl << endl;;
    return funcName;
}
