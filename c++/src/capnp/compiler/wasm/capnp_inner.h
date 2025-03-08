#pragma once

#include <string>

namespace capnp {
namespace compiler {

// Given the content of a capnproto file, compile it into a CodeGeneratorRequest,
// and return the base64 encoded message.
std::string compileSingleFile(std::string fileContent);

}
}
