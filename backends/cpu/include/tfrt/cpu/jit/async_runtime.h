/*
 * Copyright 2020 The TensorFlow Runtime Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//===- async_runtime.h ------------------------------------------*- C++ -*-===//
//
// MLIR Async Runtime implemented on top of TFRT HostContext and host
// concurrency primitives.
//
//===----------------------------------------------------------------------===//

#ifndef TFRT_BACKENDS_CPU_JIT_ASYNC_RUNTIME_H_
#define TFRT_BACKENDS_CPU_JIT_ASYNC_RUNTIME_H_

#include <cstddef>

#include "llvm/ADT/STLExtras.h"
#include "mlir/ExecutionEngine/AsyncRuntime.h"
#include "tfrt/host_context/async_dispatch.h"
#include "tfrt/host_context/async_value_ref.h"
#include "tfrt/host_context/chain.h"
#include "tfrt/support/forward_decls.h"

namespace tfrt {
namespace cpu {
namespace jit {

// Forward declare a base class for async runtime objects.
class AsyncRuntimeObject;

class AsyncRuntime {
 public:
  using Token = ::mlir::runtime::AsyncToken;
  using Value = ::mlir::runtime::AsyncValue;
  using Group = ::mlir::runtime::AsyncGroup;

  explicit AsyncRuntime(HostContext* host_context)
      : host_context_(host_context) {}

  // Creates a new token in not-ready state.
  Token* CreateToken();

  // Switched the token to the available state and runs all the awaiters.
  void SetAvailable(Token* token);

  // Blocks the caller thread until the token becomes ready.
  void AwaitToken(Token* token);

  // Creates a new value in not-ready state with a storage of the given size.
  Value* CreateValue(size_t size, size_t alignment);

  // Returns a pointer to the async value storage.
  void* GetValueStorage(Value* value);

  // Switches the value to the available state and runs all the awaiters.
  void SetAvailable(Value* value);

  // Blocks the caller thread until the value becomes ready.
  void AwaitValue(Value* value);

  // Creates a new empty group.
  Group* CreateGroup();

  // Adds `token` to the `group`.
  size_t AddTokenToGroup(Group* group, Token* token);

  // Blocks the caller thread until the group becomes ready (all tokens that
  // were added to the group are emplaced).
  void AwaitGroup(Group* group);

  // Execute the callable `f` on a thread managed by the runtime.
  template <typename F>
  void Execute(F&& f);

  // Await operation that do not block the caller thread, but instead execute
  // the callable `F` when the token/group become ready.
  template <typename F>
  void AwaitToken(Token* token, F&& f);
  template <typename F>
  void AwaitValue(Value* value, F&& f);
  template <typename F>
  void AwaitGroup(Group* group, F&& f);

  // Extracts async value that is owned by the token.
  static AsyncValue* GetAsyncValue(Token* token);

  // Extracts async value that is owned by the value.
  static AsyncValue* GetAsyncValue(Value* value);

  // Extracts async values that are owned by the tokens added to the group.
  static SmallVector<AsyncValue*, 4> GetAsyncValues(Group* group);

  // Reference counting operations for the runtime objects.
  static void AddRef(AsyncRuntimeObject* obj, unsigned count = 1);
  static void DropRef(AsyncRuntimeObject* obj, unsigned count = 1);

  // Convert Token/Value/Group to AsyncRuntimeObject*;
  static AsyncRuntimeObject* ToAsyncRuntimeObject(Token* token);
  static AsyncRuntimeObject* ToAsyncRuntimeObject(Group* group);

 private:
  HostContext* host_context_;  // must outlive *this
};

// A base class for all Async dialect types reference counted at runtime.
class AsyncRuntimeObject : public ::tfrt::ReferenceCounted<AsyncRuntimeObject> {
 public:
  virtual ~AsyncRuntimeObject() = default;
};

template <typename F>
void AsyncRuntime::Execute(F&& f) {
  EnqueueWork(host_context_, std::forward<F>(f));
}

template <typename F>
void AsyncRuntime::AwaitToken(Token* token, F&& f) {
  AsyncRuntime::GetAsyncValue(token)->AndThen(std::forward<F>(f));
}

template <typename F>
void AsyncRuntime::AwaitValue(Value* value, F&& f) {
  AsyncRuntime::GetAsyncValue(value)->AndThen(std::forward<F>(f));
}

template <typename F>
void AsyncRuntime::AwaitGroup(Group* group, F&& f) {
  RunWhenReady(AsyncRuntime::GetAsyncValues(group), std::forward<F>(f));
}

}  // namespace jit
}  // namespace cpu
}  // namespace tfrt

#endif  // TFRT_BACKENDS_CPU_JIT_ASYNC_RUNTIME_H_
