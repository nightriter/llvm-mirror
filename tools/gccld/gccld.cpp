//===----------------------------------------------------------------------===//
// LLVM 'GCCLD' UTILITY 
//
// This utility is intended to be compatible with GCC, and follows standard
// system ld conventions.  As such, the default output file is ./a.out.
// Additionally, this program outputs a shell script that is used to invoke LLI
// to execute the program.  In this manner, the generated executable (a.out for
// example), is directly executable, whereas the bytecode file actually lives in
// the a.out.bc file generated by this program.  Also, Force is on by default.
//
// Note that if someone (or a script) deletes the executable program generated,
// the .bc file will be left around.  Considering that this is a temporary hack,
// I'm not to worried about this.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/Linker.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Bytecode/Reader.h"
#include "llvm/Bytecode/WriteBytecodePass.h"
#include "llvm/Transforms/CleanupGCCOutput.h"
#include "llvm/Transforms/ConstantMerge.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "Support/CommandLine.h"
#include "Support/Signals.h"
#include <fstream>
#include <memory>
#include <algorithm>
#include <sys/types.h>     // For FileExists
#include <sys/stat.h>
using std::cerr;

cl::StringList InputFilenames("", "Load <arg> files, linking them together", 
			      cl::OneOrMore);
cl::String OutputFilename("o", "Override output filename", cl::NoFlags,"a.out");
cl::Flag   Verbose       ("v", "Print information about actions taken");
cl::StringList LibPaths  ("L", "Specify a library search path", cl::ZeroOrMore);
cl::StringList Libraries ("l", "Specify libraries to link to", cl::ZeroOrMore);
cl::Flag       Strip     ("s", "Strip symbol info from executable");

// FileExists - Return true if the specified string is an openable file...
static inline bool FileExists(const std::string &FN) {
  struct stat StatBuf;
  return stat(FN.c_str(), &StatBuf) != -1;
}

// LoadFile - Read the specified bytecode file in and return it.  This routine
// searches the link path for the specified file to try to find it...
//
static inline std::auto_ptr<Module> LoadFile(const std::string &FN) {
  std::string Filename = FN;
  std::string ErrorMessage;

  unsigned NextLibPathIdx = 0;
  bool FoundAFile = false;

  while (1) {
    if (Verbose) cerr << "Loading '" << Filename << "'\n";
    if (FileExists(Filename)) FoundAFile = true;
    Module *Result = ParseBytecodeFile(Filename, &ErrorMessage);
    if (Result) return std::auto_ptr<Module>(Result);   // Load successful!

    if (Verbose) {
      cerr << "Error opening bytecode file: '" << Filename << "'";
      if (ErrorMessage.size()) cerr << ": " << ErrorMessage;
      cerr << "\n";
    }
    
    if (NextLibPathIdx == LibPaths.size()) break;
    Filename = LibPaths[NextLibPathIdx++] + "/" + FN;
  }

  if (FoundAFile)
    cerr << "Bytecode file '" << FN << "' corrupt!  "
         << "Use 'gccld -v ...' for more info.\n";
  else
    cerr << "Could not locate bytecode file: '" << FN << "'\n";
  return std::auto_ptr<Module>();
}


int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, " llvm linker for GCC\n",
			      cl::EnableSingleLetterArgValue |
			      cl::DisableSingleLetterArgGrouping);
  assert(InputFilenames.size() > 0 && "OneOrMore is not working");

  unsigned BaseArg = 0;
  std::string ErrorMessage;

  if (!Libraries.empty()) {
    // Sort libraries list...
    std::sort(Libraries.begin(), Libraries.end());

    // Remove duplicate libraries entries...
    Libraries.erase(unique(Libraries.begin(), Libraries.end()),
                    Libraries.end());

    // Add all of the libraries to the end of the link line...
    for (unsigned i = 0; i < Libraries.size(); ++i)
      InputFilenames.push_back("lib" + Libraries[i] + ".bc");
  }

  std::auto_ptr<Module> Composite(LoadFile(InputFilenames[BaseArg]));
  if (Composite.get() == 0) return 1;

  for (unsigned i = BaseArg+1; i < InputFilenames.size(); ++i) {
    std::auto_ptr<Module> M(LoadFile(InputFilenames[i]));
    if (M.get() == 0) return 1;

    if (Verbose) cerr << "Linking in '" << InputFilenames[i] << "'\n";

    if (LinkModules(Composite.get(), M.get(), &ErrorMessage)) {
      cerr << "Error linking in '" << InputFilenames[i] << "': "
	   << ErrorMessage << "\n";
      return 1;
    }
  }

  // In addition to just linking the input from GCC, we also want to spiff it up
  // a little bit.  Do this now.
  //
  PassManager Passes;

  // Linking modules together can lead to duplicated global constants, only keep
  // one copy of each constant...
  //
  Passes.add(createConstantMergePass());

  // If the -s command line option was specified, strip the symbols out of the
  // resulting program to make it smaller.  -s is a GCC option that we are
  // supporting.
  //
  if (Strip)
    Passes.add(createSymbolStrippingPass());

  // Often if the programmer does not specify proper prototypes for the
  // functions they are calling, they end up calling a vararg version of the
  // function that does not get a body filled in (the real function has typed
  // arguments).  This pass merges the two functions.
  //
  Passes.add(createFunctionResolvingPass());

  // Now that composite has been compiled, scan through the module, looking for
  // a main function.  If main is defined, mark all other functions internal.
  //
  Passes.add(createInternalizePass());

  // Now that we have optimized the program, discard unreachable functions...
  //
  Passes.add(createGlobalDCEPass());

  // Add the pass that writes bytecode to the output file...
  std::ofstream Out((OutputFilename+".bc").c_str());
  if (!Out.good()) {
    cerr << "Error opening '" << OutputFilename << ".bc' for writing!\n";
    return 1;
  }
  Passes.add(new WriteBytecodePass(&Out));        // Write bytecode to file...

  // Make sure that the Out file gets unlink'd from the disk if we get a SIGINT
  RemoveFileOnSignal(OutputFilename+".bc");

  // Run our queue of passes all at once now, efficiently.
  Passes.run(*Composite.get());
  Out.close();

  // Output the script to start the program...
  std::ofstream Out2(OutputFilename.c_str());
  if (!Out2.good()) {
    cerr << "Error opening '" << OutputFilename << "' for writing!\n";
    return 1;
  }
  Out2 << "#!/bin/sh\nlli -q $0.bc $*\n";
  Out2.close();
  
  // Make the script executable...
  chmod(OutputFilename.c_str(), 0755);

  return 0;
}
