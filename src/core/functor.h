/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open Source License].
 * You may obtain a copy of [Open Source License] at: [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 04, 2023
 * Supported by: National Key Research and Development Program of China
 */

/**
 * The keyword 'func' is used to define a functor.
 * The concept of 'functor' is similar to C++ Functor,
 * which has few relationship with Haskell Functor.
 * You can simply think of it as a function with its own entity(or data),
 * or an entity with its own functional behavior.
 * In PSL, a functor can be treated as an entity,
 * so it can be passed as a parameter to other functors.
 * 
 * The keyword 'with' is used to define the data(meta data) of a functor.
 * You can think of it as the member variables of a callable class,
 * or you can simply treat it as template parameters like in C++.
 * 
 * The keyword 'return' is used to return a value from a functor.
 * 
 * In PSL, functors has some modifiers, which are:
 * 1. 'inner' modifier, which means the functor is a inner functor.
 *    When the PSL interpreter is encountering a inner functor,
 *    it simply ignore the detailed rest definitions of the functor,
 *    but try to find and execute the functor implemented inside the interpreter.
 *    The inner functors are used to provide the basic functions of the interpreter,
 *    which are challenging(or impossible in some cases) to be implemented by PSL itself.
 *    'inner' is typically used in standard library, and should not be used in user code.
 * 2. 'outer' modifier, which allows the functor to be defined through Python interface.
 *    This modifier allows users to define the functor in Python, and then use it in PSL.
 *    By doing so, you can manipulate the entity in PSL with the power of Python.
 *    Like handling the 'inner' modifier, the interpreter will only try to find a valid path
 *    indicating the real implementation in Python, and then ignore the rest definitions.
 *    You can simply write the path as a string literal in the functor body.
 * 3. 'sync' modifier, which means the functor is executed synchronously.
 *    By default, the PSL interpreter will execute functors asynchronously,
 *    which means it will not wait for a functor to finish to continue the interpretation.
 *    Essentially, the interpreter is designed to analyze and construct a computation graph
 *    that represents the data dependencies among functors. This computation graph is then
 *    sent to PhotEngine for parallel execution. However, it does not execute the functors in the
 *    exact order they are arranged in the source code, nor does it run them only in a single thread.
 *    The PSL interpreter is insensitive to the order of functors in code, and you are
 *    encouraged to write the functors in the order that is most readable to you.
 *    However, in some cases, you may want to execute a functor synchronously.
 *    For example, you may want to wait for a functor to finish to determine the next step.
 *    In such cases, you can use the 'sync' modifier to force the interpreter to execute those
 *    functors instantly upon encountering it and to follow the order defined in code precisely.
 *    Note that the sync functor will block the interpretation and may cause performance issues.
 *    Make sure you have resolved all the dependencies among functors before using sync functors.
 * 4. 'scoped' modifier, which means the functor will hide its entities from the outer scope.
 *    By default, the entity defined inside a functor is visible to the outer scope, which means
 *    you can access the inner data of a functor from the outer scope, or even in other functors.
 *    If a functor is defined with 'scoped' modifier, it will not share the same scope with others,
 *    and it will act like an atomic functional unit which hides its all details inside.
 *    'scoped' is useful when publishing a functor for others to use without disturbing their namespace.
 *    By the way, a scoped functor will be rendered as a single node in the graphics user interface.
 * 5. 'static' modifier, which means the functor shared the meta data among all instances.
 *    In PSL, a functor is a callable object. You can create multiple instances of a functor,
 *    with each instance has its own unique meta data. The PSL interpreter will create a new instance
 *    of a functor every time it encounters the functor in the source code, automatically by default.
 *    However, in some cases, you may want to share the meta data among all instances of a functor,
 *    or you may want to create a functor without copying the redundant and unnecessary meta data.
 *    In such cases, you can use the 'static' modifier to prevent the redundant default copying.
 *    Note that the 'static' modifier is only useful when the functor is defined with 'with' keyword.
 * 6. 'atomic' modifier, which means the functor will be executed atomically.
 *    In PSL, functors can be nested, but the interpreter does not execute them atomically by default.
 *    The interpreter will look deep into nested functors trying to extract the data dependencies out,
 *    and then construct a single integrated computation graph for further optimized scheduling.
 *    Despite this, you may force the interpreter to execute a functor atomically by using 'atomic',
 *    which means the interpreter will not extract dependencies out of it, but execute it as a whole.
 *    Note that the inner, outer and sync functors are naturally atomic, even without an 'atomic'.
 * 
 * It's necessary to point out, that modifiers may seem complex, but they essentially serve as explicit
 * definitions of the interpreter's behaviour and offer choices for many default behaviors of functors.
 * In most cases, the default behavior of functors is sufficient to meet the needs of ordinary users,
 * and users are not required to understand the modifiers to use the PSL language.
 * These modifiers are more like documentation for advanced users to fully understand the language,
 * and somehow, part of design for the developers to make the language more flexible and extensible.
 */