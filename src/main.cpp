// xfer - program entry point.
//
// Responsibilities:
//   1) Parse argc/argv into cli::Options.
//   2) Dispatch on opts.mode to the appropriate runner (help/version/send/recv).
//   3) Translate runner return values to the process exit code.
//
// Exit code convention:
//   0: success
//   2: argument error or connection failure
//   3: transfer-side failure (CRC mismatch, I/O error, etc.)

#include <cstdio>
#include <cstdlib>

#include "cli.h"
#include "xfer/version.h"

int main(int argc, char** argv) {
  xfer::cli::Options opts;
  if (!xfer::cli::Parse(argc, argv, opts)) return 2;

  switch (opts.mode) {
    case xfer::cli::Mode::Help:
      xfer::cli::PrintHelp();
      return 0;
    case xfer::cli::Mode::Version:
      std::printf("xfer %s\n", XFER_VERSION_STR);
      return 0;
    case xfer::cli::Mode::Send:
      return xfer::cli::RunSend(opts);
    case xfer::cli::Mode::Receive:
      return xfer::cli::RunReceive(opts);
    default:
      // No subcommand was given — print help and exit non-zero.
      xfer::cli::PrintHelp();
      return 2;
  }
}
