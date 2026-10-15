/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * What a function of the AST builder returns, and where the nodes it creates are allocated.
 *
 * Both exist for speed and change nothing in the tree that is built.
 *
 * Value is `std::any` with room inside: a node pointer, a pair of them or a vector of them is
 * stored in the value itself. `std::any` of libstdc++ holds one machine word, so every node a
 * builder function returned cost an allocation and a release.
 *
 * NodeArena hands out the memory of the nodes and of their loads (two objects per AST node) from
 * large blocks. Every object allocated from an arena keeps the arena alive, so the blocks are
 * released when the last node of the tree is: a tree is given up as a whole, and keeping one node
 * of it keeps the memory of all of them.
 */

#pragma once

#include <any>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace camel::parse::ast {

class Value {
  public:
    Value() noexcept = default;

    template <
        typename T, typename D = std::decay_t<T>,
        typename = std::enable_if_t<!std::is_same_v<D, Value>>>
    Value(T &&value) : manage_(&manager<D>), type_(&typeid(D)) { // NOLINT: implicit, as any is
        if (kInline<D>) {
            ::new (static_cast<void *>(buffer_)) D(std::forward<T>(value));
        } else {
            *reinterpret_cast<D **>(buffer_) = new D(std::forward<T>(value));
        }
    }

    Value(Value &&other) noexcept : manage_(other.manage_), type_(other.type_) {
        if (manage_) {
            manage_(Op::Move, &other, this);
            other.manage_ = nullptr;
            other.type_   = nullptr;
        }
    }

    Value &operator=(Value &&other) noexcept {
        if (this != &other) {
            reset();
            manage_ = other.manage_;
            type_   = other.type_;
            if (manage_) {
                manage_(Op::Move, &other, this);
                other.manage_ = nullptr;
                other.type_   = nullptr;
            }
        }
        return *this;
    }

    Value(const Value &)            = delete;
    Value &operator=(const Value &) = delete;

    ~Value() { reset(); }

    void reset() noexcept {
        if (manage_) {
            manage_(Op::Destroy, this, nullptr);
            manage_ = nullptr;
            type_   = nullptr;
        }
    }

    bool has_value() const noexcept { return manage_ != nullptr; }
    const std::type_info &type() const noexcept { return type_ ? *type_ : typeid(void); }

    /// The value held, which must be a T (std::bad_any_cast otherwise, as with std::any).
    template <typename T> T &as() {
        if (manage_ != &manager<T>) {
            throw std::bad_any_cast();
        }
        return *pointer<T>();
    }

  private:
    enum class Op { Destroy, Move };

    static constexpr size_t kCapacity = 32;

    template <typename D>
    static constexpr bool kInline = sizeof(D) <= kCapacity &&
                                    alignof(D) <= alignof(std::max_align_t) &&
                                    std::is_nothrow_move_constructible_v<D>;

    template <typename D> D *pointer() noexcept {
        return kInline<D> ? reinterpret_cast<D *>(buffer_) : *reinterpret_cast<D **>(buffer_);
    }

    template <typename D> static void manager(Op op, Value *self, Value *target) {
        if constexpr (kInline<D>) {
            D *object = reinterpret_cast<D *>(self->buffer_);
            if (op == Op::Move) {
                ::new (static_cast<void *>(target->buffer_)) D(std::move(*object));
            }
            object->~D();
        } else {
            D *&object = *reinterpret_cast<D **>(self->buffer_);
            if (op == Op::Move) {
                *reinterpret_cast<D **>(target->buffer_) = object;
            } else {
                delete object;
            }
        }
    }

    alignas(std::max_align_t) unsigned char buffer_[kCapacity];
    void (*manage_)(Op, Value *, Value *) = nullptr;
    const std::type_info *type_           = nullptr;
};

/// The value of a builder function as a T: `value_cast<Reference>(visitIdentRef(...))`.
template <typename T> T value_cast(Value &&value) { return std::move(value.template as<T>()); }
template <typename T> T value_cast(Value &value) { return value.template as<T>(); }

class NodeArena {
  public:
    NodeArena()                             = default;
    NodeArena(const NodeArena &)            = delete;
    NodeArena &operator=(const NodeArena &) = delete;

    void *allocate(size_t bytes) {
        bytes = (bytes + kAlign - 1) & ~(kAlign - 1);
        if (bytes > static_cast<size_t>(end_ - cursor_)) {
            const size_t size = bytes > kBlock ? bytes : kBlock;
            blocks_.emplace_back(static_cast<std::byte *>(::operator new(size)));
            cursor_ = blocks_.back().get();
            end_    = cursor_ + size;
        }
        void *result = cursor_;
        cursor_ += bytes;
        return result;
    }

  private:
    static constexpr size_t kAlign = alignof(std::max_align_t);
    static constexpr size_t kBlock = 64 * 1024;

    struct Free {
        void operator()(std::byte *block) const { ::operator delete(block); }
    };
    std::vector<std::unique_ptr<std::byte, Free>> blocks_;
    std::byte *cursor_ = nullptr;
    std::byte *end_    = nullptr;
};

/// Allocator over an arena, for std::allocate_shared. Releasing an object gives nothing back;
/// the arena goes when the last object allocated from it does.
template <typename T> class NodeArenaAllocator {
  public:
    using value_type = T;

    explicit NodeArenaAllocator(std::shared_ptr<NodeArena> arena) noexcept
        : arena_(std::move(arena)) {}
    template <typename U>
    NodeArenaAllocator(const NodeArenaAllocator<U> &other) noexcept : arena_(other.arena_) {}

    T *allocate(size_t count) { return static_cast<T *>(arena_->allocate(count * sizeof(T))); }
    void deallocate(T *, size_t) noexcept {}

    template <typename U> bool operator==(const NodeArenaAllocator<U> &other) const noexcept {
        return arena_ == other.arena_;
    }

  private:
    template <typename U> friend class NodeArenaAllocator;
    std::shared_ptr<NodeArena> arena_;
};

} // namespace camel::parse::ast
