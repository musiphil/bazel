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
#ifndef DEVTOOLS_BLAZE_MAIN_BLAZE_STARTUP_OPTIONS_H_
#define DEVTOOLS_BLAZE_MAIN_BLAZE_STARTUP_OPTIONS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace blaze {

using std::string;

struct StartupOptions;

// This class holds the parsed startup options for Blaze.
// These options and their defaults must be kept in sync with those
// in java/com/google/devtools/build/lib/blaze/BlazeServerStartupOptions.
// The latter are purely decorative (they affect the help message,
// which displays the defaults).  The actual defaults are defined
// in the constructor.
//
// TODO(bazel-team): The encapsulation is not quite right -- there are some
// places in blaze.cc where some of these fields are explicitly modified. Their
// names also don't conform to the style guide.
class BlazeStartupOptions {
 public:
  enum Architecture { k32Bit, k64Bit };

  BlazeStartupOptions();
  BlazeStartupOptions(const BlazeStartupOptions &rhs);
  ~BlazeStartupOptions();
  BlazeStartupOptions& operator=(const BlazeStartupOptions &rhs);

  void InitDefaults(const string& argv0);

  // Parses a single argument, either from the command line or from the .blazerc
  // "startup" options.
  //
  // rcfile should be an empty string if the option being parsed does not come
  // from a blazerc.
  //
  // Returns true if arg is unary and uses the "--foo bar" style, so its value
  // is in next_arg.
  //
  // Returns false if arg is either nullary (e.g. "--[no]batch") or is unary
  // but uses the "--foo=bar" style.
  bool ProcessArg(const string& arg, const string& next_arg,
                  const string& rcfile);

  // Adds any other options needed to result.
  void AddExtraOptions(std::vector<string> *result);

  // Checks if Blaze needs to be re-executed.  Does not return, if so.
  void CheckForReExecuteOptions(int argc, const char *argv[]);

  // Checks extra fields when processing arg.
  bool ProcessArgExtra(
      const char *arg, const char *next_arg, const string &rcfile,
      const char **value);

  // Return the default path to the JDK used to run Blaze itself
  // (must be an absolute directory).
  string GetDefaultHostJavabase();

  Architecture GetBlazeArchitecture();

  string GetJvm();

  // Adds JVM tuning flags for Blaze.
  void AddJVMSpecificArguments(const string &host_javabase,
                               std::vector<string> *result);

  // Specify architecture for testing.
  void AddJVMArchArguments(bool is_64, std::vector<string> *result);

  // Blaze's output base.  Everything is relative to this.  See
  // http://wiki/Main/BlazeOutputDirectoryStructure for details.
  string output_base;

  // Installation base for a specific release installation.
  string install_base;

  // The toplevel directory containing Blaze's output.  When Blaze is
  // run by a test, we use TEST_TMPDIR, simplifying the correct
  // hermetic invocation of Blaze from tests.
  string output_root;

  // Blaze's output_user_root. Used only for computing install_base and
  // output_base.
  string output_user_root;

  // Block for the Blaze server lock. Otherwise,
  // quit with non-0 exit code if lock can't
  // be acquired immediately.
  bool block_for_lock;

  bool host_jvm_debug;

  string host_jvm_profile;

  string host_javabase;

  string host_jvm_args;

  bool use_blaze64;

  bool batch;

  // From the man page: "This policy is useful for workloads that are
  // non-interactive, but do not want to lower their nice value, and for
  // workloads that want a deterministic scheduling policy without
  // interactivity causing extra preemptions (between the workload's tasks)."
  bool batch_cpu_scheduling;

  // If negative, don't mess with ionice. Otherwise, set a level from 0-7
  // for best-effort scheduling. 0 is highest priority, 7 is lowest. The
  // anticipatory scheduler may only honor up to priority 4. See
  // http://wiki/Main/UsingIOPriority.
  int io_nice_level;

  int max_idle_secs;

  string skyframe;

  // Temporary experimental flag that permits configurable attribute syntax
  // in BUILD files. This will be removed when configurable attributes is
  // a more stable feature.
  bool allow_configurable_attributes;

  // Temporary flag for enabling EventBus exceptions to be fatal.
  bool fatal_event_bus_exceptions;

  // A string to string map specifying where each option comes from. If the
  // value is empty, it was on the command line, if it is a string, it comes
  // from a blazerc file, if a key is not present, it is the default.
  std::map<string, string> option_sources;

  std::unique_ptr<StartupOptions> extra_options;

 private:
  // Sets default values for members.
  void Init();

  // Copies member variables from rhs to lhs. This cannot use the compiler-
  // generated copy constructor because extra_options is a unique_ptr and
  // unique_ptr deletes its copy constructor.
  void Copy(const BlazeStartupOptions &rhs, BlazeStartupOptions *lhs);
};

}  // namespace blaze
#endif  // DEVTOOLS_BLAZE_MAIN_BLAZE_STARTUP_OPTIONS_H_