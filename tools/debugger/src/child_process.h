// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file child_process.h
 * @brief A child process with its standard output and error read by the parent.
 *
 * Both users of worker processes go through this: the HTTP gateway (spawn.cpp), which keeps
 * the output of a worker as that task's log, and the debug adapter (dap/adapter.cpp), which
 * forwards it to the editor as output events. The child dies with the parent: on Windows by a
 * job object closed with the parent, on Linux by PR_SET_PDEATHSIG, elsewhere when the parent
 * terminates its children on exit (terminateAll).
 *
 * The POSIX implementation is the one that is exercised by the tests; the Windows one is a
 * port of the CreateProcess code the gateway had and has not been run since it was moved here.
 */

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace debugger {

struct ProcessSpec {
    std::string exe;
    std::vector<std::string> args; // without argv[0]
    /// Variables added to (or replacing those of) the parent's environment.
    std::vector<std::pair<std::string, std::string>> env;
    std::string cwd; // empty: the parent's
};

struct ProcessCallbacks {
    /// Called on a reader thread with each chunk as it arrives (not line-buffered).
    std::function<void(std::string_view)> onStdout;
    /// When null, standard error goes to onStdout as well, in arrival order.
    std::function<void(std::string_view)> onStderr;
    /// Called once, after both streams are at their end: the exit code, or -1 and the signal
    /// that killed the process.
    std::function<void(int exitCode, int signal)> onExit;
};

class ChildProcess {
  public:
    /// Starts the process; null and a message in `error` when it could not be started.
    static std::shared_ptr<ChildProcess>
    spawn(const ProcessSpec &spec, ProcessCallbacks callbacks, std::string &error);

    virtual ~ChildProcess()      = default;
    virtual bool running() const = 0;
    /// Ends the process at once (SIGKILL / TerminateProcess). Returns whether it was running.
    virtual bool kill() = 0;
    /// Waits until the process has exited and onExit has returned; false on timeout.
    virtual bool waitExit(int timeoutMs) = 0;

    /// Kills every child of this process that is still running (for exit paths).
    static void terminateAll();
};

/// A TCP port on 127.0.0.1 that was free a moment ago; 0 when none could be found.
int pickFreeLocalPort();

} // namespace debugger
