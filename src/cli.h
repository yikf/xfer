// xfer - cli: command-line argument parsing and subcommand dispatch.
//
// Command grammar:
//   xfer send  [--host HOST] [-p PORT] [-r] [-L] [--no-zero-copy] [--no-progress]
//              <file-or-dir>...
//   xfer recv  [-p PORT] [-o DIR] [--no-verify] [--no-progress]
//   xfer --help / -h
//   xfer --version / -V
//
// Global options:
//   -v, --verbose     Enable DEBUG logging
//   --no-progress     Disable inline progress bar
//   --color=MODE      always|auto|never — controls log colorization
//
// Design: Parse populates `Options`; the caller dispatches on `opts.mode`.
// This separation simplifies unit-testing the parser independently from the
// network transport.

#ifndef XFER_CLI_H
#define XFER_CLI_H

#include <string>
#include <system_error>
#include <vector>

namespace xfer {
namespace cli {

enum class Mode {
  Unset,
  Send,
  Receive,
  Help,
  Version,
};

// Full set of parsed command-line options.
struct Options {
  Mode mode = Mode::Unset;

  // Common options
  bool verbose = false;
  bool no_progress = false;
  bool force_color = false;
  int port = 9876;

  // Sender options
  std::string host;            // Remote host; empty => sender listens for connections
  bool listen = false;         // Listen on `port` and wait for receiver to connect
  bool recursive = false;      // Descend into directories
  bool follow_symlinks = false;// Follow symlinks during directory walk
  bool no_zero_copy = false;   // Disable sendfile()-style zero-copy paths
  std::vector<std::string> paths;

  // Receiver options
  std::string output_dir;      // Output directory; empty = current working directory
  bool no_verify = false;      // Disable per-file CRC-32 verification
};

// Parse argc/argv into `opts`. On success returns true; on error writes a
// one-line diagnostic to stderr and returns false.
bool Parse(int argc, char** argv, Options& opts);

// Print the canonical help text to stdout.
void PrintHelp();

// Entry points: run a send or receive session end-to-end. Returns the process
// exit code (0 = success, >0 = failure).
int RunSend(const Options& opts);
int RunReceive(const Options& opts);

}  // namespace cli
}  // namespace xfer

#endif  // XFER_CLI_H
