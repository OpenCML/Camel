// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "child_process.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
extern char **environ;
#endif

namespace debugger {

namespace {

// The registry is never destroyed: terminateAll runs from an atexit handler, which main
// registers before the first process exists, so it runs after the destructors of statics
// made later (a function-local static here would be gone by then).
std::mutex &registryMutex() {
    static auto *m = new std::mutex;
    return *m;
}
std::vector<std::weak_ptr<ChildProcess>> &registry() {
    static auto *r = new std::vector<std::weak_ptr<ChildProcess>>;
    return *r;
}

/// State shared by both implementations: the exit notification.
class ProcessBase : public ChildProcess {
  public:
    bool running() const override { return !exited_.load(); }
    bool waitExit(int timeoutMs) override {
        std::unique_lock<std::mutex> lock(mutex_);
        return cond_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return exitNotified_;
        });
    }

  protected:
    void notifyExit(const ProcessCallbacks &callbacks, int code, int signal) {
        exited_.store(true);
        if (callbacks.onExit) {
            callbacks.onExit(code, signal);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exitNotified_ = true;
        }
        cond_.notify_all();
    }

    std::atomic<bool> exited_{false};
    std::mutex mutex_;
    std::condition_variable cond_;
    bool exitNotified_ = false;
};

#if !defined(_WIN32)

void setCloseOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

void readAll(int fd, const std::function<void(std::string_view)> &sink) {
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            if (sink) {
                sink(std::string_view(buf, static_cast<size_t>(n)));
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(fd);
}

void writeErrno(int fd, int value) {
    ssize_t ignored = ::write(fd, &value, sizeof(value));
    (void)ignored;
}

class PosixProcess final : public ProcessBase {
  public:
    bool kill() override {
        if (exited_.load()) {
            return false;
        }
        return ::kill(pid_, SIGKILL) == 0;
    }

    static std::shared_ptr<PosixProcess>
    start(const ProcessSpec &spec, ProcessCallbacks callbacks, std::string &error) {
        int outPipe[2]         = {-1, -1};
        int errPipe[2]         = {-1, -1};
        int execPipe[2]        = {-1, -1};
        const bool separateErr = static_cast<bool>(callbacks.onStderr);
        // execPipe is closed by a successful exec; the child writes errno to it otherwise.
        if (::pipe(outPipe) != 0 || (separateErr && ::pipe(errPipe) != 0) ||
            ::pipe(execPipe) != 0) {
            error = std::string("pipe: ") + std::strerror(errno);
            return nullptr;
        }
        setCloseOnExec(execPipe[1]);

        // Everything the child needs is prepared before fork: between fork and exec only
        // async-signal-safe calls are made (the parent has other threads).
        std::vector<std::string> argStore;
        argStore.push_back(spec.exe);
        for (const auto &a : spec.args) {
            argStore.push_back(a);
        }
        std::vector<char *> argv;
        for (auto &a : argStore) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);

        std::vector<std::string> envStore;
        for (char **e = environ; e && *e; ++e) {
            std::string_view entry(*e);
            std::string_view name = entry.substr(0, entry.find('='));
            bool replaced         = false;
            for (const auto &kv : spec.env) {
                if (name == kv.first) {
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                envStore.emplace_back(entry);
            }
        }
        for (const auto &kv : spec.env) {
            envStore.push_back(kv.first + "=" + kv.second);
        }
        std::vector<char *> envp;
        for (auto &e : envStore) {
            envp.push_back(e.data());
        }
        envp.push_back(nullptr);

        const pid_t parent = ::getpid();
        const pid_t pid    = ::fork();
        if (pid < 0) {
            error = std::string("fork: ") + std::strerror(errno);
            return nullptr;
        }
        if (pid == 0) {
#if defined(__linux__)
            ::prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (::getppid() != parent) {
                ::_exit(127); // the parent died in between
            }
#else
            (void)parent;
#endif
            ::dup2(outPipe[1], STDOUT_FILENO);
            ::dup2(separateErr ? errPipe[1] : outPipe[1], STDERR_FILENO);
            int devNull = ::open("/dev/null", O_RDONLY);
            if (devNull >= 0) {
                ::dup2(devNull, STDIN_FILENO);
            }
            for (int fd = 3; fd < 1024; ++fd) {
                if (fd != execPipe[1]) {
                    ::close(fd);
                }
            }
            if (!spec.cwd.empty() && ::chdir(spec.cwd.c_str()) != 0) {
                writeErrno(execPipe[1], errno);
                ::_exit(127);
            }
            ::execve(spec.exe.c_str(), argv.data(), envp.data());
            writeErrno(execPipe[1], errno);
            ::_exit(127);
        }

        ::close(outPipe[1]);
        if (separateErr) {
            ::close(errPipe[1]);
        }
        ::close(execPipe[1]);
        int childErrno = 0;
        ssize_t got;
        do {
            got = ::read(execPipe[0], &childErrno, sizeof(childErrno));
        } while (got < 0 && errno == EINTR);
        ::close(execPipe[0]);
        if (got == static_cast<ssize_t>(sizeof(childErrno))) {
            ::close(outPipe[0]);
            if (separateErr) {
                ::close(errPipe[0]);
            }
            int status = 0;
            ::waitpid(pid, &status, 0);
            error = "cannot start " + spec.exe + ": " + std::strerror(childErrno);
            return nullptr;
        }

        auto proc  = std::make_shared<PosixProcess>();
        proc->pid_ = pid;
        setCloseOnExec(outPipe[0]);
        if (separateErr) {
            setCloseOnExec(errPipe[0]);
        }
        // The waiter owns the readers: the exit is reported after both streams ended, so
        // that no output arrives after onExit.
        const int out = outPipe[0];
        const int err = separateErr ? errPipe[0] : -1;
        std::thread([proc, callbacks = std::move(callbacks), out, err]() {
            std::thread errReader;
            if (err >= 0) {
                errReader = std::thread([&callbacks, err] { readAll(err, callbacks.onStderr); });
            }
            readAll(out, callbacks.onStdout);
            if (errReader.joinable()) {
                errReader.join();
            }
            int status = 0;
            pid_t r;
            do {
                r = ::waitpid(proc->pid_, &status, 0);
            } while (r < 0 && errno == EINTR);
            int code = -1, signal = 0;
            if (r > 0 && WIFEXITED(status)) {
                code = WEXITSTATUS(status);
            } else if (r > 0 && WIFSIGNALED(status)) {
                signal = WTERMSIG(status);
            }
            proc->notifyExit(callbacks, code, signal);
        }).detach();
        return proc;
    }

  private:
    pid_t pid_ = -1;
};

#else // _WIN32

HANDLE sharedJob() {
    // Closing the job (when this process ends, however it ends) terminates what is in it.
    static HANDLE job = [] {
        HANDLE h = CreateJobObjectA(nullptr, nullptr);
        if (h != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
            info.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return h;
    }();
    return job;
}

// Quoting as CommandLineToArgvW undoes it.
std::string quoteArg(const std::string &arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
        return arg;
    }
    std::string out    = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
        } else {
            out.append(backslashes, '\\');
        }
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

void readAll(HANDLE h, const std::function<void(std::string_view)> &sink) {
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
        if (sink) {
            sink(std::string_view(buf, n));
        }
    }
    CloseHandle(h);
}

class WindowsProcess final : public ProcessBase {
  public:
    bool kill() override {
        if (exited_.load()) {
            return false;
        }
        return TerminateProcess(process_, 1) != 0;
    }

    static std::shared_ptr<WindowsProcess>
    start(const ProcessSpec &spec, ProcessCallbacks callbacks, std::string &error) {
        const bool separateErr = static_cast<bool>(callbacks.onStderr);
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
        if (!CreatePipe(&outR, &outW, &sa, 0)) {
            error = "CreatePipe failed";
            return nullptr;
        }
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
        if (separateErr) {
            if (!CreatePipe(&errR, &errW, &sa, 0)) {
                CloseHandle(outR);
                CloseHandle(outW);
                error = "CreatePipe failed";
                return nullptr;
            }
            SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
        }

        std::string cmdLine = quoteArg(spec.exe);
        for (const auto &a : spec.args) {
            cmdLine += ' ';
            cmdLine += quoteArg(a);
        }

        // The environment block: the parent's entries not overridden, then the additions.
        std::string envBlock;
        if (char *cur = GetEnvironmentStringsA()) {
            for (const char *p = cur; *p != '\0';) {
                std::string entry(p);
                p += entry.size() + 1;
                const size_t eq = entry.find('=', 1);
                bool replaced   = false;
                for (const auto &kv : spec.env) {
                    if (eq != std::string::npos &&
                        _stricmp(entry.substr(0, eq).c_str(), kv.first.c_str()) == 0) {
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    envBlock += entry;
                    envBlock += '\0';
                }
            }
            FreeEnvironmentStringsA(cur);
        }
        for (const auto &kv : spec.env) {
            envBlock += kv.first + "=" + kv.second;
            envBlock += '\0';
        }
        envBlock += '\0';

        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');
        STARTUPINFOA si        = {};
        si.cb                  = sizeof(si);
        si.dwFlags             = STARTF_USESTDHANDLES;
        si.hStdInput           = nullptr;
        si.hStdOutput          = outW;
        si.hStdError           = separateErr ? errW : outW;
        PROCESS_INFORMATION pi = {};
        const BOOL ok          = CreateProcessA(
            nullptr,
            cmdBuf.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            static_cast<LPVOID>(envBlock.data()),
            spec.cwd.empty() ? nullptr : spec.cwd.c_str(),
            &si,
            &pi);
        CloseHandle(outW);
        if (separateErr) {
            CloseHandle(errW);
        }
        if (!ok) {
            CloseHandle(outR);
            if (separateErr) {
                CloseHandle(errR);
            }
            error = "cannot start " + spec.exe + " (CreateProcess error " +
                    std::to_string(GetLastError()) + ")";
            return nullptr;
        }
        CloseHandle(pi.hThread);
        if (HANDLE job = sharedJob()) {
            AssignProcessToJobObject(job, pi.hProcess);
        }

        auto proc      = std::make_shared<WindowsProcess>();
        proc->process_ = pi.hProcess;
        std::thread([proc, callbacks = std::move(callbacks), outR, errR]() {
            std::thread errReader;
            if (errR != nullptr) {
                errReader = std::thread([&callbacks, errR] { readAll(errR, callbacks.onStderr); });
            }
            readAll(outR, callbacks.onStdout);
            if (errReader.joinable()) {
                errReader.join();
            }
            WaitForSingleObject(proc->process_, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(proc->process_, &code);
            proc->notifyExit(callbacks, static_cast<int>(code), 0);
        }).detach();
        return proc;
    }

    ~WindowsProcess() override {
        if (process_ != nullptr) {
            CloseHandle(process_);
        }
    }

  private:
    HANDLE process_ = nullptr;
};

#endif

} // namespace

std::shared_ptr<ChildProcess>
ChildProcess::spawn(const ProcessSpec &spec, ProcessCallbacks callbacks, std::string &error) {
#if defined(_WIN32)
    std::shared_ptr<ChildProcess> proc = WindowsProcess::start(spec, std::move(callbacks), error);
#else
    std::shared_ptr<ChildProcess> proc = PosixProcess::start(spec, std::move(callbacks), error);
#endif
    if (proc) {
        std::lock_guard<std::mutex> lock(registryMutex());
        auto &all = registry();
        std::erase_if(all, [](const std::weak_ptr<ChildProcess> &w) { return w.expired(); });
        all.push_back(proc);
    }
    return proc;
}

void ChildProcess::terminateAll() {
    std::vector<std::shared_ptr<ChildProcess>> live;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        for (const auto &w : registry()) {
            if (auto p = w.lock()) {
                live.push_back(std::move(p));
            }
        }
    }
    for (const auto &p : live) {
        p->kill();
    }
}

int pickFreeLocalPort() {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        return 0;
    }
    sockaddr_in addr     = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    int port             = 0;
    int len              = sizeof(addr);
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0 &&
        getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
    closesocket(s);
    return port;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return 0;
    }
    sockaddr_in addr     = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    int port             = 0;
    socklen_t len        = sizeof(addr);
    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0 &&
        ::getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
    ::close(s);
    return port;
#endif
}

} // namespace debugger
