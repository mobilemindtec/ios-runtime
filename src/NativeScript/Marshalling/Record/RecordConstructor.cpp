//
//  RecordConstructor.cpp
//  NativeScript
//
//  Created by Jason Zhekov on 9/27/14.
//  Copyright (c) 2014 Telerik. All rights reserved.
//

#include "RecordConstructor.h"
#include "Interop.h"
#include "JSErrors.h"
#include "RecordField.h"
#include "RecordInstance.h"
#include "RecordPrototype.h"
#include <JavaScriptCore/inspector/JSGlobalObjectInspectorController.h>
#include <sstream>

namespace NativeScript {
using namespace JSC;

const ClassInfo RecordConstructor::s_info = { "record", &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(RecordConstructor) };

static bool areEqual(ExecState* execState, JSValue v1, JSValue v2, JSCell* typeCell) {
    JSC::VM& vm = execState->vm();
    if (RecordConstructor* recordConstructor = jsDynamicCast<RecordConstructor*>(execState->vm(), typeCell)) {
        if (!(v1.isObject() && v2.isObject())) {
            return false;
        }

        if (v1.inherits(vm, RecordInstance::info()) && v2.inherits(vm, RecordInstance::info())) {
            RecordInstance* record1 = jsCast<RecordInstance*>(v1);
            RecordInstance* record2 = jsCast<RecordInstance*>(v2);

            if (record1->size() != record2->size()) {
                return false;
            }

            bool areEqual = memcmp(record1->data(), record2->data(), record1->size()) == 0;
            return areEqual;
        } else {
            if (!(v1.isObject() && v2.isObject())) {
                return false;
            }

            RecordPrototype* recordPrototype = jsDynamicCast<RecordPrototype*>(vm, recordConstructor->get(execState, vm.propertyNames->prototype));
            for (RecordField* field : recordPrototype->fields()) {
                Identifier fieldName = Identifier::fromString(execState, field->fieldName());

                auto scope = DECLARE_THROW_SCOPE(vm);

                JSValue fieldValue1 = v1.get(execState, fieldName);
                if (scope.exception()) {
                    return false;
                }

                JSValue fieldValue2 = v2.get(execState, fieldName);
                if (scope.exception()) {
                    return false;
                }

                if (!areEqual(execState, fieldValue1, fieldValue2, field->fieldType())) {
                    return false;
                }
            }

            return true;
        }
    } else {
        return JSValue::equal(execState, v1, v2);
    }
}

static EncodedJSValue JSC_HOST_CALL recordConstructorFuncEquals(ExecState* execState) {
    JSValue arg1 = execState->argument(0);
    JSValue arg2 = execState->argument(1);
    JSC::VM& vm = execState->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (execState->argumentCount() != 2) {
        return JSValue::encode(scope.throwException(execState, createError(execState, "Two arguments required."_s)));
    }

    RecordConstructor* recordConstructor = jsCast<RecordConstructor*>(execState->thisValue());

    bool result = areEqual(execState, arg1, arg2, recordConstructor);
    return JSValue::encode(jsBoolean(result));
}

JSValue RecordConstructor::read(ExecState* execState, const void* buffer, JSCell* self) {
    GlobalObject* globalObject = jsCast<GlobalObject*>(execState->lexicalGlobalObject());
    RecordConstructor* constructor = jsCast<RecordConstructor*>(self);
    const size_t size = constructor->_ffiTypeMethodTable.ffiType->size;

    void* data = malloc(size);
    memcpy(data, buffer, size);
    PointerInstance* pointer = jsCast<PointerInstance*>(globalObject->interop()->pointerInstanceForPointer(execState, data));
    pointer->setAdopted(true);
    RecordInstance* record = RecordInstance::create(execState->vm(), globalObject, constructor->instancesStructure(), size, pointer);
    return record;
}

void RecordConstructor::write(ExecState* execState, const JSValue& value, void* buffer, JSCell* self) {
    RecordConstructor* constructor = jsCast<RecordConstructor*>(self);
    const ffi_type* ffiType = constructor->_ffiTypeMethodTable.ffiType;

    if (RecordInstance* record = jsDynamicCast<RecordInstance*>(execState->vm(), value)) {
        if (ffiType != jsCast<RecordConstructor*>(record->get(execState, execState->vm().propertyNames->constructor))->_ffiTypeMethodTable.ffiType) {
            JSValue exception = createError(execState, "Different record types");
            reportFatalErrorBeforeShutdown(execState, Exception::create(execState->vm(), exception));
        }

        memcpy(buffer, record->data(), record->size());
    } else {
        memset(buffer, 0, ffiType->size);

        JSC::VM& vm = execState->vm();
        auto scope = DECLARE_THROW_SCOPE(vm);

        if (JSObject* object = jsDynamicCast<JSObject*>(execState->vm(), value)) {
            const WTF::Vector<RecordField*> fields = jsCast<RecordPrototype*>(constructor->get(execState, execState->vm().propertyNames->prototype))->fields();
            for (RecordField* field : fields) {
                Identifier propertyName = Identifier::fromString(execState, field->fieldName());
                if (object->hasProperty(execState, propertyName)) {
                    JSValue fieldValue = object->get(execState, propertyName);
                    if (scope.exception()) {
                        return;
                    }

                    field->ffiTypeMethodTable().write(execState, fieldValue, reinterpret_cast<void*>(reinterpret_cast<char*>(buffer) + field->offset()), field->fieldType());
                    if (scope.exception()) {
                        return;
                    }
                }
            }
        } else if (!value.isUndefinedOrNull()) {
            JSValue exception = createError(execState, WTF::String::format("Could not marshall \"%s\" to \"%s\" type.", value.toWTFString(execState).utf8().data(), constructor->name().utf8().data()));
            throwVMError(execState, scope, exception);
            return;
        }
    }
}

bool RecordConstructor::canConvert(ExecState* execState, const JSValue& value, JSCell* self) {
    JSC::VM& vm = execState->vm();
    return value.isObject() || value.inherits(vm, RecordInstance::info());
}

const char* RecordConstructor::encode(VM& vm, JSCell* cell) {
    RecordConstructor* self = jsCast<RecordConstructor*>(cell);

    if (!self->_compilerEncoding.empty()) {
        return self->_compilerEncoding.c_str();
    }

    std::stringstream ss;
    ss << (self->_recordType == RecordType::Struct ? "{" : "(");
    ss << jsCast<JSString*>(self->getDirect(vm, vm.propertyNames->name))->tryGetValue().utf8().data() << "=";

    RecordPrototype* recordPrototype = jsCast<RecordPrototype*>(self->getDirect(vm, vm.propertyNames->prototype));
    for (RecordField* field : recordPrototype->fields()) {
        ss << field->ffiTypeMethodTable().encode(vm, field->fieldType());
    }

    ss << (self->_recordType == RecordType::Struct ? "}" : ")");
    self->_compilerEncoding = ss.str();
    return self->_compilerEncoding.c_str();
}

void RecordConstructor::finishCreation(VM& vm, JSGlobalObject* globalObject, RecordPrototype* recordPrototype, const WTF::String& name, ffi_type* ffiType, RecordType recordType) {
    Base::finishCreation(vm, name.characterAt(0) == '?' ? WTF::emptyString() : name);

    this->_ffiTypeMethodTable.ffiType = ffiType;
    this->_ffiTypeMethodTable.read = &read;
    this->_ffiTypeMethodTable.write = &write;
    this->_ffiTypeMethodTable.canConvert = &canConvert;
    this->_ffiTypeMethodTable.encode = &encode;
    this->_recordType = recordType;

    this->putDirect(vm, vm.propertyNames->prototype, recordPrototype, PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly);
    this->putDirect(vm, vm.propertyNames->length, jsNumber(1), PropertyAttribute::ReadOnly | PropertyAttribute::DontEnum | PropertyAttribute::DontDelete);
    this->putDirectNativeFunction(vm, globalObject, Identifier::fromString(&vm, "equals"_s), 0, recordConstructorFuncEquals, NoIntrinsic, PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly);

    this->_instancesStructure.set(vm, this, RecordInstance::createStructure(globalObject, recordPrototype));
}

EncodedJSValue JSC_HOST_CALL RecordConstructor::constructRecordInstance(ExecState* execState) {
    GlobalObject* globalObject = jsCast<GlobalObject*>(execState->lexicalGlobalObject());
    RecordConstructor* constructor = jsCast<RecordConstructor*>(execState->callee().asCell());
    const ffi_type* ffiType = constructor->_ffiTypeMethodTable.ffiType;

    void* data = calloc(ffiType->size, 1);
    PointerInstance* pointer = jsCast<PointerInstance*>(globalObject->interop()->pointerInstanceForPointer(execState, data));
    pointer->setAdopted(true);

    RecordInstance* instance = RecordInstance::create(execState->vm(), globalObject, constructor->instancesStructure(), ffiType->size, pointer);

    if (execState->argumentCount() == 1) {
        JSValue value = execState->argument(0);

        if (PointerInstance* pointerArgument = jsDynamicCast<PointerInstance*>(execState->vm(), value)) {
            memcpy(pointer->data(), pointerArgument->data(), ffiType->size);
        } else {
            constructor->_ffiTypeMethodTable.write(execState, value, instance->data(), constructor);
        }
    }

    return JSValue::encode(instance);
}

EncodedJSValue JSC_HOST_CALL RecordConstructor::createRecordInstance(ExecState* execState) {
    JSC::VM& vm = execState->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (execState->argumentCount() != 1) {
        return JSValue::encode(scope.throwException(execState, createError(execState, "One argument required."_s)));
    }

    const JSValue value = execState->argument(0);
    if (!value.inherits(vm, PointerInstance::info())) {
        const WTF::String message = WTF::String::format("Argument must be a %s.", PointerInstance::info()->className);
        return JSValue::encode(scope.throwException(execState, createError(execState, message)));
    }

    PointerInstance* pointer = jsCast<PointerInstance*>(value);
    GlobalObject* globalObject = jsCast<GlobalObject*>(execState->lexicalGlobalObject());
    RecordConstructor* constructor = jsCast<RecordConstructor*>(execState->callee().asCell());
    const ffi_type* ffiType = constructor->_ffiTypeMethodTable.ffiType;

    RecordInstance* instance = RecordInstance::create(execState->vm(), globalObject, constructor->instancesStructure(), ffiType->size, pointer);
    return JSValue::encode(instance);
}

void RecordConstructor::visitChildren(JSCell* cell, SlotVisitor& visitor) {
    Base::visitChildren(cell, visitor);

    RecordConstructor* object = jsCast<RecordConstructor*>(cell);
    visitor.append(object->_instancesStructure);
}

RecordConstructor::~RecordConstructor() {
    delete this->_ffiTypeMethodTable.ffiType->elements;
    delete this->_ffiTypeMethodTable.ffiType;
}
} // namespace NativeScript
