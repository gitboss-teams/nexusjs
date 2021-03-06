/*
 * Nexus.js - The next-gen JavaScript platform
 * Copyright (C) 2016  Abdullah A. Hassan <abdullah@webtomizer.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "nexus.h"

#include <stdexcept>
#include <utility>

#include <wtf/FastMalloc.h>
#include <WTF/wtf/StackTrace.h>
#include "object.h"
#include "value.h"
#include "exception.h"
#include "context.h"

#include "scoped_string.h"

NX::Object::Object (JSContextRef context, JSClassRef cls, void * data): myContext(context), myObject(nullptr)
{
  myObject = JSObjectMake(context, cls, data);
  attach();
}

NX::Object::Object (JSContextRef context, JSObjectRef obj): myContext(context), myObject(obj)
{
  attach();
}

NX::Object::Object (JSContextRef context, JSValueRef val): myContext(context), myObject(nullptr)
{
  JSValueRef exception = nullptr;
  myObject = JSValueToObject(myContext, val, &exception);
  if (exception) {
    throw NX::Exception(myContext, exception);
  }
  attach();
}

NX::Object::Object (const NX::Object & other): myContext(other.myContext), myObject(other.myObject)
{
  attach();
}

NX::Object::Object (JSContextRef context, time_t val): myContext(context), myObject(nullptr)
{
  JSValueRef args[] {
    NX::Value(myContext, val * 1000).value()
  };
  JSValueRef exception = nullptr;
  myObject = JSObjectMakeDate(myContext, 1, args, &exception);
  if (exception) {
    throw NX::Exception(myContext, exception);
  }
  attach();
}

NX::Object::Object (JSContextRef context, const std::exception & e): myContext(context), myObject(nullptr)
{
  WTF::StringPrintStream ss;
  ss.printf("%s\n", e.what());
  if (auto * nxp = dynamic_cast<const NX::Exception*>(&e)) {
    nxp->trace()->dump(ss, "\t");
  } else {
    auto trace = WTF::StackTrace::captureStackTrace(10);
    trace->dump(ss, "\t");
  }
  ss.printf("\n");
  NX::Value message(myContext, ss.toString().utf8().data());
  JSValueRef args[] { message.value() };
  myObject = JSObjectMakeError(myContext, 1, args, nullptr);
  attach();
}

NX::Object::Object (JSContextRef context, const boost::system::error_code & e): myContext(context), myObject(nullptr)
{
  NX::Value message(myContext, e.message());
  JSValueRef args[] { message.value() };
  myObject = JSObjectMakeError(myContext, 1, args, nullptr);
  attach();
}


NX::Object::~Object()
{
  clear();
}

std::shared_ptr<NX::Value> NX::Object::operator[] (const char * name)
{
  if (!myObject) return std::shared_ptr<NX::Value>(nullptr);
  NX::ScopedString nameRef(name);
  JSValueRef exception = nullptr;
  JSValueRef val = JSObjectGetProperty(myContext, myObject, nameRef, &exception);
  if (exception) {
    NX::Value except(myContext, exception);
    throw NX::Exception(except.toString());
  }
  return std::make_shared<NX::Value>(myContext, val);
}

std::shared_ptr<NX::Value> NX::Object::operator[] (unsigned int index)
{
  if (!myObject) return std::shared_ptr<NX::Value>(nullptr);
  JSValueRef exception;
  JSValueRef val = JSObjectGetPropertyAtIndex(myContext, myObject, index, &exception);
  if (exception) {
    NX::Value except(myContext, exception);
    throw NX::Exception(except.toString());
  }
  return std::make_shared<NX::Value>(myContext, val);
}

std::shared_ptr<NX::Value> NX::Object::operator[] (const char * name) const
{
  if (!myObject) return std::shared_ptr<NX::Value>(nullptr);
  NX::ScopedString nameRef(name);
  JSValueRef exception = nullptr;
  JSValueRef val = JSObjectGetProperty(myContext, myObject, nameRef, &exception);
  if (exception) {
    NX::Value except(myContext, exception);
    throw NX::Exception(except.toString());
  }
  return std::make_shared<NX::Value>(myContext, val);
}

std::shared_ptr<NX::Value> NX::Object::operator[] (unsigned int index) const
{
  if (!myObject) return std::shared_ptr<NX::Value>(nullptr);
  JSValueRef exception;
  JSValueRef val = JSObjectGetPropertyAtIndex(myContext, myObject, index, &exception);
  if (exception) {
    NX::Value except(myContext, exception);
    throw NX::Exception(except.toString());
  }
  return std::make_shared<NX::Value>(myContext, val);
}

std::string NX::Object::toString()
{
  NX::Value val(myContext, myObject);
  return val.toString();
}

NX::Object NX::Object::then(NX::Object::PromiseCallback onResolve, NX::Object::PromiseCallback onReject)
{
  if (!myObject)
    throw NX::Exception("not a promise");
  struct PromiseData {
    PromiseCallback onResolve;
    PromiseCallback onReject;
  };
  auto promiseData = new PromiseData { onResolve, onReject };
  JSObjectRef dataCarrier = JSObjectMake(myContext, NX::Context::FromJsContext(myContext)->nexus()->genericClass(), promiseData);
  JSObjectSetPrivate(dataCarrier, promiseData);
  JSValueRef exp = nullptr;
  JSValueRef resolve = onResolve ? JSBindFunction(myContext, JSObjectMakeFunctionWithCallback(myContext, ScopedString("onResolve"),
                                                                                              [](JSContextRef ctx, JSObjectRef function,
                                                                                                 JSObjectRef thisObject, size_t argumentCount,
                                                                                                 const JSValueRef arguments[],
                                                                                                 JSValueRef* exception) -> JSValueRef
  {
    auto promiseData = reinterpret_cast<PromiseData*>(JSObjectGetPrivate(thisObject));
    if (promiseData) {
      JSValueRef val = promiseData->onResolve(ctx, arguments[0], exception);
      delete promiseData;
      return val;
    } else
      return *exception =  NX::Exception("empty promise data").toError(ctx);
  }), dataCarrier, 0, nullptr, &exp) : JSValueMakeUndefined(myContext);
  if (exp)
  {
    NX::Nexus::ReportException(myContext, exp);
  }
  JSValueRef reject = onReject ? JSBindFunction(myContext, JSObjectMakeFunctionWithCallback(myContext, ScopedString("onReject"),
                                                                                            [](JSContextRef ctx, JSObjectRef function,
                                                                                               JSObjectRef thisObject, size_t argumentCount,
                                                                                               const JSValueRef arguments[],
                                                                                               JSValueRef* exception) -> JSValueRef
  {
    auto promiseData = reinterpret_cast<PromiseData*>(JSObjectGetPrivate(thisObject));
    if (promiseData) {
      JSValueRef val = promiseData->onReject(ctx, arguments[0], exception);
      delete promiseData;
      return val;
    } else
      return *exception =  NX::Exception("empty promise data").toError(ctx);
  }), dataCarrier, 0, nullptr, &exp) : JSValueMakeUndefined(myContext);
  if (exp)
  {
    NX::Nexus::ReportException(myContext, exp);
  }
  return NX::Object(myContext, operator[]("then")->toObject()->call(myObject, std::vector<JSValueRef> { resolve, reject }, nullptr));
}

void NX::Object::push(JSValueRef value, JSValueRef *exception) {
  this->operator[]("push")->toObject()->call(this->value(), { value }, exception);
}

JSValueRef NX::Object::await() {
  auto context = NX::Context::FromJsContext(myContext);
  auto scheduler = context->nexus()->scheduler();
  JSValueRef result = nullptr, exception = nullptr;
  this->then([&](JSContextRef ctx, JSValueRef res, JSValueRef*) {
    result = res;
    JSValueProtect(context->toJSContext(), result);
    return JSValueMakeUndefined(ctx);
  }, [&](JSContextRef ctx, JSValueRef exp, JSValueRef*) {
    exception = exp;
    JSValueProtect(context->toJSContext(), exp);
    return JSValueMakeUndefined(ctx);
  });
  while(!result && !exception)
    scheduler->yield();
  if (exception) {
    JSValueUnprotect(context->toJSContext(), exception);
  }
  if (result) {
    JSValueUnprotect(context->toJSContext(), result);
  }
  if (exception)
    throw NX::Exception(context->toJSContext(), exception);
  return result;
}

NX::Object::Object(JSContextRef context, const std::vector<JSValueRef> &values): myContext(context), myObject(nullptr)
{
  JSValueRef exp = nullptr;
  if (!values.empty())
    myObject = JSObjectMakeArray(context, values.size(), values.begin().operator->(), &exp);
  else
    myObject = JSObjectMakeArray(context, 0, nullptr, &exp);
  if (exp) {
    NX::Value except(myContext, exp);
    throw NX::Exception(except.toString());
  }
  attach();
}

bool NX::Object::toBoolean() {
  if (myObject)
    return JSValueToBoolean(myContext, myObject);
  else
    return false;
}

NX::Object::Object(NX::Object &&other) noexcept:
  myContext(other.myContext), myObject(other.myObject)
{
  other.myContext = nullptr;
  other.myObject = nullptr;
  attach();
}

void NX::Object::clear() {
  if (myContext && myObject) {
    JSValueUnprotect(myContext, myObject);
  }
  myContext = nullptr;
  myObject = nullptr;
}

void NX::Object::attach() {
  if (myContext && myObject)
    JSValueProtect(myContext, myObject);
}
