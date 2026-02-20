/**
 * camel-profiler: 独立性能分析入口，链接 libcamel。
 * 可用于单独启动/停止 advanced tracing 或查看报告。
 * 注：advanced profiler 仅在 Debug 构建中启用（#ifndef NDEBUG）。
 */
#include <iostream>
#include <string>

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

int main(int argc, char *argv[]) {
    std::string mode = (argc > 1) ? argv[1] : "help";
    if (mode == "-h" || mode == "--help" || mode == "help") {
        std::cerr << "Usage: camel-profiler <start|stop|report>\n"
                     "  start  - start advanced tracing (output to profile_reports/)\n"
                     "  stop   - stop tracing\n"
                     "  report - generate report from current trace\n";
        return 0;
    }

#ifndef NDEBUG
    if (mode == "start") {
        profiler::AdvancedTracer::Config config;
        config.enablePerfettoIntegration = true;
        config.perfettoOutput = "profile_reports/camel_trace.perfetto-trace";
        config.outputFile     = "profile_reports/camel_trace.json";
        profiler::start_advanced_tracing(config);
        std::cout << "Profiling started. Run 'camel-profiler stop' when done.\n";
        return 0;
    }
    if (mode == "stop") {
        profiler::stop_advanced_tracing();
        std::cout << "Profiling stopped.\n";
        return 0;
    }
    if (mode == "report") {
        profiler::generate_advanced_report();
        std::cout << "Report generated.\n";
        return 0;
    }
#else
    (void)mode;
    std::cerr << "camel-profiler: advanced profiler is disabled in Release builds. "
                 "Use Debug build for profiling.\n";
    return 1;
#endif

    std::cerr << "Unknown command: " << mode << "\n";
    return 1;
}
