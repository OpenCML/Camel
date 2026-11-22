// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A host run of one kernel body: the statement driver, the element bytes, and the one rounding.
//
// The scalar expressions evaluate themselves (`ScalarExpr::evaluate`), so what is left here is the
// part that touches memory: which element an address names, how wide that element is, and what a
// value written back into a narrow element is. Everything else -- loops, conditions, faults -- is the
// body's own structure, executed in the order it was written.

#include "tiles/internal/serial_execute.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace camel::tiles::kernel {
namespace {
using Index = std::unordered_map<std::string, uint64_t>;
using Registers = std::unordered_map<std::string, ScalarValue>;

bool known(ScalarType type) {
    return std::any_of(
        serialElementTypes().begin(), serialElementTypes().end(),
        [&](ScalarType each) { return each == type; });
}

size_t width(ScalarType type) {
    switch (type.kind) {
    case ScalarType::F16:
    case ScalarType::BF16:
        return 2;
    case ScalarType::F32:
    case ScalarType::I32:
    case ScalarType::U32:
        return 4;
    case ScalarType::F64:
    case ScalarType::I64:
        return 8;
    }
    throw std::runtime_error("serial execution: invalid element type");
}

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(std::string("serial execution: ") + message);
}

/// One thread's execution of the body: the loop and index names it holds, the registers it wrote,
/// and the buffers it reads and writes. A thread run is sequential, so the body's own order is the
/// whole of its meaning here; the threads of a serial run never overlap.
class Thread {
  public:
    Thread(
        const KernelBody &body, std::vector<SerialBuffer> &buffers,
        std::vector<SerialWrite> &writes, uint32_t &fault)
        : body(body), buffers(buffers), writes(writes), fault(fault) {}

    void run(const Index &start) {
        index = start;
        block(body.statements);
    }

  private:
    const KernelBody &body;
    std::vector<SerialBuffer> &buffers;
    std::vector<SerialWrite> &writes;
    uint32_t &fault;
    Index index;
    Registers registers;
    bool returned = false;

    /// The bindings a scalar expression reads: the registers this thread wrote, plus every loop and
    /// index name as the i64 position the expression layer reads them as.
    std::unordered_map<std::string, ScalarValue> bindings() const {
        auto result = registers;
        for (const auto &[name, position] : index)
            result.insert_or_assign(name, ScalarValue::i64(static_cast<int64_t>(position)));
        return result;
    }

    void block(const Block &statements) {
        for (const auto &statement : statements) {
            if (returned)
                return;
            std::visit([&](const auto &each) { execute(each); }, statement);
        }
    }

    // ---- statements -------------------------------------------------------------------------
    void execute(const IndexBinding &s) { index.insert_or_assign(s.name, s.value.evaluate(index)); }

    void execute(const ScalarBinding &s) { write(s.name, s.value.evaluate(bindings())); }

    void execute(const Assignment &s) {
        require(registers.contains(s.name), "assignment to an undeclared register");
        write(s.name, s.value.evaluate(bindings()));
    }

    void execute(const Load &s) {
        // A masked-out access reads nothing at all: the fallback is the value the body declared.
        const auto at = s.source.evaluate(index);
        if (!at) {
            write(s.name, s.fallback);
            return;
        }
        write(s.name, read(buffers.at(s.source.buffer), *at));
    }

    void execute(const Store &s) {
        const auto at = s.target.evaluate(index);
        if (!at)
            return;
        write(s.target.buffer, *at, s.value.evaluate(bindings()));
    }

    void execute(const Loop &s) {
        const auto begin = s.begin.evaluate(index), end = s.end.evaluate(index);
        for (uint64_t at = begin; at < end; at += s.step) {
            index.insert_or_assign(s.index, at);
            block(s.body);
            if (returned)
                return;
        }
    }

    void execute(const Conditional &s) {
        const bool taken = std::visit(
            [&](const auto &condition) {
                using T = std::decay_t<decltype(condition)>;
                // An index predicate is evaluated over the addresses; a scalar predicate over the
                // registers. Neither is turned into the other.
                if constexpr (std::is_same_v<T, Predicate>)
                    return condition.evaluate(index);
                else
                    return condition.evaluate(bindings());
            },
            s.condition);
        block(taken ? s.body : s.otherwise);
    }

    // A serial run executes one thread to completion before the next, so a barrier is already
    // satisfied: there is no other thread between the store before it and the load after it.
    void execute(const Barrier &) {}

    void execute(const ReportFault &s) {
        fault = fault == 0 || s.source < fault ? s.source : fault;
    }

    void execute(const Return &) { returned = true; }

    // ---- element bytes ----------------------------------------------------------------------
    ScalarValue read(const SerialBuffer &buffer, uint64_t address) {
        const auto size = width(buffer.type);
        const auto at   = static_cast<size_t>(address) * size;
        require(at + size <= buffer.bytes.size(), "read outside the buffer");
        const auto *from = buffer.bytes.data() + at;
        switch (size) {
        case 2: {
            uint16_t bits = 0;
            std::memcpy(&bits, from, 2);
            return {buffer.type, bits};
        }
        case 4: {
            uint32_t bits = 0;
            std::memcpy(&bits, from, 4);
            return {buffer.type, bits};
        }
        default: {
            uint64_t bits = 0;
            std::memcpy(&bits, from, 8);
            return {buffer.type, bits};
        }
        }
    }

    void write(size_t bufferIndex, uint64_t address, const ScalarValue &value) {
        auto &buffer = buffers.at(bufferIndex);
        require(value.type == buffer.type, "store value does not fit the element");
        const auto size = width(buffer.type);
        const auto at   = static_cast<size_t>(address) * size;
        require(at + size <= buffer.bytes.size(), "write outside the buffer");
        auto *to = buffer.bytes.data() + at;
        switch (size) {
        case 2:
            std::memcpy(to, &value.bits, 2);
            break;
        case 4:
            std::memcpy(to, &value.bits, 4);
            break;
        default:
            std::memcpy(to, &value.bits, 8);
            break;
        }
        writes.push_back({bufferIndex, address, value});
    }

    void write(const std::string &name, ScalarValue value) {
        registers.insert_or_assign(name, std::move(value));
    }
};
} // namespace

const std::vector<ScalarType> &serialElementTypes() {
    // Every element type a plan can name. A type that is not in this list is refused by name rather
    // than executed at a neighbour's width, so adding one to the type layer without adding it here
    // is a refusal a check can count instead of a wrong number nobody notices.
    static const std::vector<ScalarType> types = {
        ScalarType::F32, ScalarType::F64, ScalarType::F16, ScalarType::BF16,
        ScalarType::I32, ScalarType::U32, ScalarType::I64};
    return types;
}

SerialRun serialExecute(
    const KernelBody &body, const SerialGrid &grid, std::vector<SerialBuffer> buffers) {
    require(buffers.size() == body.buffers.size(), "buffer count does not match the body");
    for (size_t i = 0; i < buffers.size(); ++i) {
        const auto &type = buffers[i].type;
        require(known(type), "no element type for this buffer");
        require(
            buffers[i].bytes.size() == body.buffers[i].extent * width(type),
            "buffer extent does not match the body");
    }
    SerialRun run;
    run.buffers = std::move(buffers);
    for (unsigned by = 0; by < grid.gridY; ++by)
        for (unsigned bx = 0; bx < grid.gridX; ++bx)
            for (unsigned ty = 0; ty < grid.blockY; ++ty)
                for (unsigned tx = 0; tx < grid.blockX; ++tx) {
                    const Index index = {
                        {"block_x", bx},
                        {"block_y", by},
                        {"block_z", 0},
                        {"thread_x", tx},
                        {"thread_y", ty},
                        {"thread_z", 0}};
                    Thread thread(body, run.buffers, run.writes, run.fault);
                    thread.run(index);
                    ++run.threads;
                }
    run.faulted = run.fault != 0;
    return run;
}
} // namespace camel::tiles::kernel