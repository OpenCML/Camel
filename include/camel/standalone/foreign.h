/**
 * @file foreign.h
 * @brief A value the runtime owns, held by a generated program: a counted handle.
 *
 * A tensor, a PyObject and the like are not values a generated program builds itself; they live in
 * the runtime and the program holds a handle to them (docs/design/cpp-standalone/README.cn.md 9 and
 * the type table). The semantics of such a value is implemented once, in its runtime module, and the
 * generated program reaches it over the bridge -- so "the same input, the same output" is guaranteed
 * by one implementation rather than by two that must be kept in step.
 *
 * The handle keeps the runtime object alive (the runtime's rooted handle is what the pointer
 * identifies); the text a program prints for it is the runtime's own text. Both live in libcamel,
 * because only the runtime knows how to trace, print and release its own objects.
 */
#ifndef CAMEL_STANDALONE_FOREIGN_H
#define CAMEL_STANDALONE_FOREIGN_H

#include <string>
#include <utility>

namespace camel::standalone {

/// A counted handle to a value the runtime owns. Copying adds a count, moving does not, destroying
/// takes one and releases the object at zero.
class Foreign {
  public:
    constexpr Foreign() noexcept = default;
    explicit constexpr Foreign(void *handle) noexcept : handle_(handle) {}

    Foreign(const Foreign &other) : handle_(retain(other.handle_)) {}
    Foreign(Foreign &&other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

    Foreign &operator=(const Foreign &other) {
        if (this != &other) {
            void *taken = retain(other.handle_);
            release(handle_);
            handle_ = taken;
        }
        return *this;
    }

    Foreign &operator=(Foreign &&other) noexcept {
        if (this != &other) {
            release(handle_);
            handle_     = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Foreign() { release(handle_); }

    /// The handle itself: what the bridge passes and what identifies the runtime object.
    [[nodiscard]] constexpr void *handle() const noexcept { return handle_; }

    /// The text the runtime prints for the value it holds.
    [[nodiscard]] std::string text() const { return textOf(handle_); }

    static void *retain(void *handle) noexcept;
    static void release(void *handle) noexcept;
    static std::string textOf(void *handle);

  private:
    void *handle_ = nullptr;
};

/// Appends what the runtime prints for `v`, as every other `rt::` value's printer does.
inline void appendText(std::string &out, const Foreign &v) { out += v.text(); }

} // namespace camel::standalone

#endif // CAMEL_STANDALONE_FOREIGN_H