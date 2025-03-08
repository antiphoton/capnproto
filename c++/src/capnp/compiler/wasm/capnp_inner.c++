#include "src/capnp/compiler/wasm/capnp_inner.h"

#include <string>
#include <vector>

#include "src/capnp/compiler/compiler.h"
#include "src/capnp/compiler/error-reporter.h"
#include "src/capnp/compiler/module-loader.h"
#include "src/capnp/message.h"
#include "src/capnp/schema.h"
#include "src/capnp/serialize.h"
#include "src/kj/encoding.h"
#include "src/kj/filesystem.h"
#include "src/kj/io.h"

namespace capnp {
namespace compiler {

namespace {
class MyErrorReporter : public GlobalErrorReporter {
public:
  void addError(const kj::ReadableDirectory &directory, kj::PathPtr path,
                SourcePos start, SourcePos end,
                kj::StringPtr message) {
    errors.push_back(std::to_string(start.line + 1) + ':' + std::to_string(start.column + 1) + ' ' + std::string(message));
  }
  bool hadErrors() {
    return !errors.empty();
  }

  std::vector<std::string> errors;
};

struct SourceFile {
  uint64_t id;
  Compiler::ModuleScope compiled;
  kj::StringPtr name;
  Module *module;
};

kj::Vector<SourceFile> createSourceFiles(MyErrorReporter &errorReporter, Compiler *compiler, std::string content) {
  kj::Vector<SourceFile> sourceFiles;
  uint compileEagerness = Compiler::NODE | Compiler::CHILDREN |
                          Compiler::DEPENDENCIES | Compiler::DEPENDENCY_PARENTS;
  ModuleLoader loader(errorReporter);
  auto dir = kj::newInMemoryDirectory(kj::nullClock());
  auto file = dir->openFile(kj::Path("index.capnp"), kj::WriteMode::CREATE);
  file->writeAll(kj::StringPtr(content));
  KJ_IF_MAYBE (module, loader.loadModule(*dir, kj::Path("index.capnp"))) {
    auto compiled = compiler->add(*module);
    compiler->eagerlyCompile(compiled.getId(), compileEagerness);
    sourceFiles.add(SourceFile{compiled.getId(), compiled, "index.capnp", &*module});
  }
  return sourceFiles;
}

std::string encodeMessageToBase64Url(MallocMessageBuilder &&message) {
  kj::VectorOutputStream vectorOutputStream;
  writeMessage(vectorOutputStream, message.getSegmentsForOutput());
  kj::ArrayPtr<byte> byteArray = vectorOutputStream.getArray();
  kj::String output = encodeBase64Url(byteArray);
  return output.cStr();
}

// This function should be the same as `capnp::compiler::CompilerMain::generateOutput` in `capnp.c++`.
void compileMain(Compiler *compiler, const kj::Vector<SourceFile> &sourceFiles, MallocMessageBuilder &message) {
  auto request = message.initRoot<schema::CodeGeneratorRequest>();

  auto version = request.getCapnpVersion();
  version.setMajor(CAPNP_VERSION_MAJOR);
  version.setMinor(CAPNP_VERSION_MINOR);
  version.setMicro(CAPNP_VERSION_MICRO);

  auto schemas = compiler->getLoader().getAllLoaded();
  auto nodes = request.initNodes(schemas.size());
  for (size_t i = 0; i < schemas.size(); i++) {
    nodes.setWithCaveats(i, schemas[i].getProto());
  }

  request.adoptSourceInfo(compiler->getAllSourceInfo(message.getOrphanage()));

  auto requestedFiles = request.initRequestedFiles(sourceFiles.size());
  for (size_t i = 0; i < sourceFiles.size(); i++) {
    auto requestedFile = requestedFiles[i];
    requestedFile.setId(sourceFiles[i].id);
    requestedFile.setFilename(sourceFiles[i].name);
    requestedFile.adoptImports(compiler->getFileImportTable(
        *sourceFiles[i].module, Orphanage::getForMessageContaining(requestedFile)));
    // Populate FileSourceInfo with identifier resolutions, including type IDs and member details.
    auto fileSourceInfo = requestedFile.initFileSourceInfo();
    const auto resolutions = sourceFiles[i].module->getResolutions();
    auto identifiers = fileSourceInfo.initIdentifiers(resolutions.size());
    for (size_t j = 0; j < resolutions.size(); j++) {
      auto identifier = identifiers[j];
      identifier.setStartByte(resolutions[j].startByte);
      identifier.setEndByte(resolutions[j].endByte);
      KJ_SWITCH_ONEOF(resolutions[j].target) {
        KJ_CASE_ONEOF(type, Resolution::Type) {
          identifier.setTypeId(type.typeId);
        }
        KJ_CASE_ONEOF(member, Resolution::Member) {
          identifier.initMember();
          auto identifier_member = identifier.getMember();
          identifier_member.setParentTypeId(member.parentTypeId);
          identifier_member.setOrdinal(member.ordinal);
        }
      }
    }
  }
}

}

std::string compileSingleFile(std::string fileContent) {
  kj::SpaceFor<Compiler> compilerSpace;
  kj::Own<Compiler> compiler = compilerSpace.construct(Compiler::COMPILE_ANNOTATIONS);
  MyErrorReporter errorReporter;
  auto sourceFiles = createSourceFiles(errorReporter, compiler.get(), fileContent);
  if (errorReporter.hadErrors()) {
    std::string output = "Err";
    for (std::string &s : errorReporter.errors) {
      output += '\n';
      output += s;
    }
    return output;
  } else {
    MallocMessageBuilder message;
    compileMain(compiler.get(), sourceFiles, message);
    return "Ok\n" + encodeMessageToBase64Url(kj::mv(message));
  }
}

}
}
