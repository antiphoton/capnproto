#include <emscripten/bind.h>

#include "src/capnp/compiler/wasm/capnp_inner.h"

EMSCRIPTEN_BINDINGS(what) {
  emscripten::function("compileSingleFile",
                       &capnp::compiler::compileSingleFile);
}
