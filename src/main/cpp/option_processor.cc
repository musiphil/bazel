// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "option_processor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <utility>

#include "blaze_exit_code.h"
#include "blaze_util.h"
#include "util/file.h"
#include "util/strings.h"

using std::list;
using std::map;
using std::vector;

namespace blaze {

OptionProcessor::RcOption::RcOption(int rcfile_index, const string& option) {
  rcfile_index_ = rcfile_index;
  option_ = option;
}


OptionProcessor::RcFile::RcFile(const string& filename, int index) {
  filename_ = filename;
  index_ = index;
}

void OptionProcessor::RcFile::Parse(
    vector<RcFile>* rcfiles,
    map<string, vector<RcOption> >* rcoptions) {
  list<string> initial_import_stack;
  initial_import_stack.push_back(filename_);
  Parse(filename_, index_, rcfiles, rcoptions, &initial_import_stack);
}

void OptionProcessor::RcFile::Parse(
    string filename, const int index,
    vector<RcFile>* rcfiles,
    map<string, vector<RcOption> >* rcoptions,
    list<string>* import_stack) {
  string contents;
  if (!ReadFile(filename, &contents)) {
    // We checked for file readability before, so this is unexpected.
    die(blaze_exit_code::INTERNAL_ERROR,
        "Unexpected error reading .blazerc file '%s'\n", filename.c_str());
  }

  // A '\' at the end of a line continues the line.
  blaze_util::Replace("\\\r\n", "", &contents);
  blaze_util::Replace("\\\n", "", &contents);
  vector<string> startup_options;

  vector<string> lines = blaze_util::Split(contents, '\n');
  for (int line = 0; line < lines.size(); ++line) {
    blaze_util::StripWhitespace(&lines[line]);

    // Check for an empty line.
    if (lines[line].empty()) {
      continue;
    }

    vector<string> words;

    // This will treat "#" as a comment, and properly
    // quote single and double quotes, and treat '\'
    // as an escape character.
    // TODO(bazel-team): This function silently ignores
    // dangling backslash escapes and missing end-quotes.
    blaze_util::Tokenize(lines[line], '#', &words);

    if (words.empty()) {
      // Could happen if line starts with "#"
      continue;
    }

    string command = words[0];

    if (command == "import") {
      if (words.size() != 2) {
        die(blaze_exit_code::BAD_ARGV,
            "Invalid import declaration in .blazerc file '%s': '%s'\n",
            filename.c_str(), lines[line].c_str());
      }

      if (std::find(import_stack->begin(), import_stack->end(), words[1]) !=
          import_stack->end()) {
        string loop;
        for (list<string>::const_iterator imported_rc = import_stack->begin();
             imported_rc != import_stack->end(); ++imported_rc) {
          loop += "  " + *imported_rc + "\n";
        }
        die(blaze_exit_code::BAD_ARGV,
            "Import loop detected:\n%s", loop.c_str());
      }

      rcfiles->push_back(RcFile(words[1], rcfiles->size()));
      import_stack->push_back(words[1]);
      RcFile::Parse(rcfiles->back().Filename(), rcfiles->back().Index(),
                    rcfiles, rcoptions, import_stack);
      import_stack->pop_back();
    } else {
      for (int word = 1; word < words.size(); ++word) {
        (*rcoptions)[command].push_back(RcOption(index, words[word]));
        if (command == "startup") {
          startup_options.push_back(words[word]);
        }
      }
    }
  }

  if (!startup_options.empty()) {
    string startup_args;
    blaze_util::JoinStrings(startup_options, ' ', &startup_args);
    fprintf(stderr, "INFO: Reading 'startup' options from %s: %s\n",
            filename.c_str(), startup_args.c_str());
  }
}

OptionProcessor::OptionProcessor()
    : initialized_(false), parsed_startup_options_(new BlazeStartupOptions()) {
}

// Return the path of the depot .blazerc file.
string OptionProcessor::FindDepotBlazerc(const string& workspace) {
  // Package semantics are ignored here, but that's acceptable because
  // blaze.blazerc is a configuration file.
  string blazerc = blaze_util::JoinPath(workspace, "tools/blaze.blazerc");
  if (access(blazerc.c_str(), R_OK)) {
    // tools/ is probably not mapped in the client, so we peek into ../READONLY.
    // It's a little ugly, but it works.
    blazerc = blaze_util::JoinPath(
        workspace, "../READONLY/google3/tools/blaze.blazerc");
  }
  if (access(blazerc.c_str(), R_OK)) {
    blazerc = "";
  }
  return blazerc;
}

// Return the path the the user .blazerc file.
// If cmdLineRcFile != NULL, use it, dying
// it it is not readable.
// Otherwise, return the first readable file of
// [/path/to/workspace/google3/.blazerc, $HOME/.blazerc].
//
// If no readable .blazerc file is found, return the empty string.
string OptionProcessor::FindUserBlazerc(const char* cmdLineRcFile,
                                        const string& workspace) {
  if (cmdLineRcFile != NULL) {
    string rcFile = MakeAbsolute(cmdLineRcFile);
    if (access(rcFile.c_str(), R_OK)) {
      die(blaze_exit_code::BAD_ARGV,
          "Error: Unable to read .blazerc file '%s'.\n", rcFile.c_str());
    }
    return rcFile;
  }

  string workspaceRcFile = blaze_util::JoinPath(workspace, ".blazerc");
  if (!access(workspaceRcFile.c_str(), R_OK)) {
    return workspaceRcFile;
  }

  const char* home = getenv("HOME");
  if (home == NULL) {
    return "";
  }

  string userRcFile = blaze_util::JoinPath(home, ".blazerc");
  if (!access(userRcFile.c_str(), R_OK)) {
    return userRcFile;
  }
  return "";
}

void OptionProcessor::ParseOptions(const vector<string>& args,
                                   const string& workspace,
                                   const string& cwd) {
  assert(!initialized_);
  initialized_ = true;

  const char* blazerc = NULL;
  bool use_master_blazerc = true;

  // Check if there is a blazerc related option given
  args_ = args;
  for (int i= 1; i < args.size(); i++) {
    const char* arg_chr = args[i].c_str();
    const char* next_arg_chr = (i + 1) < args.size()
        ? args[i + 1].c_str()
        : NULL;
    if (blazerc == NULL) {
      blazerc = GetUnaryOption(arg_chr, next_arg_chr, "--blazerc");
    }
    if (use_master_blazerc &&
        GetNullaryOption(arg_chr, "--nomaster_blazerc")) {
      use_master_blazerc = false;
    }
  }

  // Parse depot and user blazerc files.
  // This is not a little ineffective (copying a multimap around), but it is a
  // small one and this way I don't have to care about memory management.
  if (use_master_blazerc) {
    string depot_blazerc_path = FindDepotBlazerc(workspace);
    if (!depot_blazerc_path.empty()) {
      blazercs_.push_back(RcFile(depot_blazerc_path, blazercs_.size()));
      blazercs_.back().Parse(&blazercs_, &rcoptions_);
    }
  }

  string user_blazerc_path = FindUserBlazerc(blazerc, workspace);
  if (!user_blazerc_path.empty()) {
    blazercs_.push_back(RcFile(user_blazerc_path, blazercs_.size()));
    blazercs_.back().Parse(&blazercs_, &rcoptions_);
  }

  const char* binary = (!args.empty()) ? args[0].c_str() : "";

  parsed_startup_options_->InitDefaults(binary);
  ParseStartupOptions();

  // Determine command
  if (startup_args_ + 1 >= args.size()) {
    command_ = "";
    return;
  }

  command_ = args[startup_args_ + 1];
  AddRcfileArgsAndOptions(parsed_startup_options_->batch, cwd);
  for (unsigned int cmd_arg = startup_args_ + 2;
       cmd_arg < args.size(); cmd_arg++) {
    command_arguments_.push_back(args[cmd_arg]);
  }
}

void OptionProcessor::ParseOptions(int argc, const char* argv[],
                                   const string& workspace,
                                   const string& cwd) {
  vector<string> args(argc);
  for (int arg = 0; arg < argc; arg++) {
    args[arg] = argv[arg];
  }

  return ParseOptions(args, workspace, cwd);
}

static bool IsArg(const string& arg) {
  return blaze_util::starts_with(arg, "-") && (arg != "--help")
      && (arg != "-help") && (arg != "-h");
}

void OptionProcessor::ParseStartupOptions() {
  // Process rcfile startup options
  map< string, vector<RcOption> >::const_iterator it =
      rcoptions_.find("startup");
  if (it != rcoptions_.end()) {
    const vector<RcOption>& startup_options = it->second;
    int i = 0;
    // Process all elements except the last one.
    for (; i < startup_options.size() - 1; i++) {
      const RcOption& option = startup_options[i];
      const string& blazerc = blazercs_[option.rcfile_index()].Filename();
      if (parsed_startup_options_->ProcessArg(option.option(),
          startup_options[i + 1].option(), blazerc)) {
        i++;
      }
    }
    // Process last element, if any.
    if (i < startup_options.size()) {
      const RcOption& option = startup_options[i];
      if (IsArg(option.option())) {
        const string& blazerc = blazercs_[option.rcfile_index()].Filename();
        parsed_startup_options_->ProcessArg(option.option(), "", blazerc);
      }
    }
  }

  // Process command-line args next, so they override any of the same options
  // from .blazerc. Stop on first non-arg, this includes --help
  unsigned int i = 1;
  if (!args_.empty()) {
    for (;  (i < args_.size() - 1) && IsArg(args_[i]); i++) {
      if (parsed_startup_options_->ProcessArg(args_[i], args_[i + 1], "")) {
        i++;
      }
    }
    if (i < args_.size() && IsArg(args_[i])) {
      parsed_startup_options_->ProcessArg(args_[i], "", "");
      i++;
    }
  }
  startup_args_ = i -1;
}

// Appends the command and arguments from argc/argv to the end of arg_vector,
// and also splices in some additional terminal and environment options between
// the command and the arguments. NB: Keep the options added here in sync with
// BlazeCommandDispatcher.INTERNAL_COMMAND_OPTIONS!
void OptionProcessor::AddRcfileArgsAndOptions(bool batch, const string& cwd) {
  // Push the options mapping .blazerc numbers to filenames.
  for (int i_blazerc = 0; i_blazerc < blazercs_.size(); i_blazerc++) {
    const RcFile& blazerc = blazercs_[i_blazerc];
    command_arguments_.push_back("--rc_source=" + blazerc.Filename());
  }

  // Push the option defaults
  for (map<string, vector<RcOption> >::const_iterator it = rcoptions_.begin();
       it != rcoptions_.end(); ++it) {
    if (it->first == "startup") {
      // Skip startup options, they are parsed in the C++ wrapper
      continue;
    }

    for (int ii = 0; ii < it->second.size(); ii++) {
      const RcOption& rcoption = it->second[ii];
      command_arguments_.push_back(
          "--default_override=" + std::to_string(rcoption.rcfile_index()) + ":"
          + it->first + "=" + rcoption.option());
    }
  }

  // Splice the terminal options.
  command_arguments_.push_back(
      "--isatty=" + std::to_string(IsStandardTerminal()));
  command_arguments_.push_back(
      "--terminal_columns=" + std::to_string(GetTerminalColumns()));

  // Pass the client environment to the server in server mode.
  if (batch) {
    command_arguments_.push_back("--ignore_client_env");
  } else {
    for (char** env = environ; *env != NULL; env++) {
      command_arguments_.push_back("--client_env=" + string(*env));
    }
  }
  command_arguments_.push_back("--client_cwd=" + cwd);

  const char *emacs = getenv("EMACS");
  if (emacs != NULL && strcmp(emacs, "t") == 0) {
    command_arguments_.push_back("--emacs");
  }
}

void OptionProcessor::GetCommandArguments(vector<string>* result) const {
  result->insert(result->end(),
                 command_arguments_.begin(),
                 command_arguments_.end());
}

const string& OptionProcessor::GetCommand() const {
  return command_;
}

const BlazeStartupOptions& OptionProcessor::GetParsedStartupOptions() const {
  return *parsed_startup_options_.get();
}
}  // namespace blaze