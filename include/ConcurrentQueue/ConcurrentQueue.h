#ifndef RSJ_CONCURRENT_QUEUE_H
#define RSJ_CONCURRENT_QUEUE_H
/*
==============================================================================
Copyright 2019 Rory Jaffe
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================
*/

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <type_traits>
namespace rsj {
    /* all but blocking pops use scoped_lock. blocking pops use unique_lock */
    template<typename T, class Container = std::deque<T>, class Mutex = std::mutex>
    class ConcurrentQueue {
    public:
        using container_type = Container;
        using value_type = typename Container::value_type;
        using size_type = typename Container::size_type;
        using reference = typename Container::reference;
        using const_reference = typename Container::const_reference;
        static_assert(std::is_same_v<T, value_type>, "container adapters require consistent types");
        /* Constructors: see https://en.cppreference.com/w/cpp/container/queue/queue. These are in
         * same order and number as in cppreference */
        /*1*/ ConcurrentQueue() noexcept(
            std::is_nothrow_default_constructible_v<Container>) = default;
        /*2*/ explicit ConcurrentQueue(const Container& cont) noexcept(
            std::is_nothrow_copy_constructible_v<Container>)
            : queue_{ cont }
        {
        }
        /*3*/ explicit ConcurrentQueue(Container&& cont) noexcept(
            std::is_nothrow_move_constructible_v<Container>)
            : queue_{ std::exchange(cont, {}) }
        {
        }
        /*4*/ ConcurrentQueue(const ConcurrentQueue& other) noexcept(
            std::is_nothrow_copy_constructible_v<Container> && noexcept(
                std::scoped_lock(std::declval<Mutex>())))
        {
            auto lock{ std::scoped_lock(other.mutex_) };
            queue_ = other.queue_;
        }
        /*5*/ ConcurrentQueue(ConcurrentQueue&& other) noexcept(
            std::is_nothrow_move_constructible_v<Container> && noexcept(
                std::scoped_lock(std::declval<Mutex>())))
        {
            auto lock{ std::scoped_lock(other.mutex_) };
            queue_ = std::exchange(other.queue_, {});
        }
        /*6*/ template<class Alloc, class = std::enable_if_t<std::uses_allocator_v<Container, Alloc>>>
        explicit ConcurrentQueue(const Alloc& alloc) noexcept(
            std::is_nothrow_constructible_v<Container, const Alloc&>)
            : queue_{ alloc }
        {
        }
        /*7*/ template<class Alloc, class = std::enable_if_t<std::uses_allocator_v<Container, Alloc>>>
        ConcurrentQueue(const Container& cont, const Alloc& alloc) : queue_{ cont, alloc }
        {
        }
        /*8*/ template<class Alloc, class = std::enable_if_t<std::uses_allocator_v<Container, Alloc>>>
        ConcurrentQueue(Container&& cont, const Alloc& alloc) noexcept(
            std::is_nothrow_constructible_v<Container, Container, const Alloc&>)
            : queue_(std::exchange(cont, {}), alloc)
        {
        }
        /*9*/ template<class Alloc, class = std::enable_if_t<std::uses_allocator_v<Container, Alloc>>>
        ConcurrentQueue(const ConcurrentQueue& other, const Alloc& alloc) : queue_(alloc)
        {
            auto lock{ std::scoped_lock(other.mutex_) };
            queue_ = Container(other.queue_, alloc);
        }
        /*10*/ template<class Alloc,
            class = std::enable_if_t<std::uses_allocator_v<Container, Alloc>>>
        ConcurrentQueue(ConcurrentQueue&& other, const Alloc& alloc) noexcept(
            std::is_nothrow_constructible_v<Container, Container, const Alloc&> && noexcept(
                std::scoped_lock(std::declval<Mutex>())))
            : queue_(alloc)
        {
            auto lock{ std::scoped_lock(other.mutex_) };
            queue_ = Container(std::exchange(other.queue_, {}), alloc);
        }
        /* operator= */
        ConcurrentQueue& operator=(const ConcurrentQueue& other) noexcept(
            std::is_nothrow_copy_assignable_v<Container> && noexcept(
                std::scoped_lock(std::declval<Mutex>())))
        {
            {
                auto lock{ std::scoped_lock(mutex_, other.mutex_) };
                queue_ = other.queue_;
            }
            condition_.notify_all();
            return *this;
        }
        ConcurrentQueue& operator=(ConcurrentQueue&& other) noexcept(
            std::is_nothrow_move_assignable_v<Container> && noexcept(
                std::scoped_lock(std::declval<Mutex&>())))
        {
            {
                auto lock{ std::scoped_lock(mutex_, other.mutex_) };
                queue_ = std::exchange(other.queue_, {});
            }
            condition_.notify_all();
            return *this;
        }
        /* destructor */
        ~ConcurrentQueue() = default;
        /* methods */
        [[nodiscard]] auto empty() const noexcept(noexcept(
            std::declval<Container>().empty()) && noexcept(std::scoped_lock(std::declval<Mutex&>())))
        {
            auto lock{ std::scoped_lock(mutex_) };
            return queue_.empty();
        }
        [[nodiscard]] size_type size() const noexcept(noexcept(
            std::declval<Container>().size()) && noexcept(std::scoped_lock(std::declval<Mutex>())))
        {
            auto lock{ std::scoped_lock(mutex_) };
            return queue_.size();
        }
        [[nodiscard]] size_type max_size() const
            noexcept(noexcept(std::declval<Container>().max_size()) && noexcept(
                std::scoped_lock(std::declval<Mutex>())))
        {
            auto lock{ std::scoped_lock(mutex_) };
            return queue_.max_size();
        }
        void push(const T& value)
        {
            {
                auto lock{ std::scoped_lock(mutex_) };
                queue_.push_back(value);
            }
            condition_.notify_one();
        }
        void push(T&& value)
        {
            {
                auto lock{ std::scoped_lock(mutex_) };
                queue_.push_back(std::move(value));
            }
            condition_.notify_one();
        }
        template<class... Args> void emplace(Args&&... args)
        {
            {
                T new_item{ std::forward<Args>(args)... };
                auto lock{ std::scoped_lock(mutex_) };
                queue_.push_back(std::move(new_item));
            }
            condition_.notify_one();
        }
        T pop()
        {
            auto lock{ std::unique_lock(mutex_) };
            while (queue_.empty()) { condition_.wait(lock); }
            T rc{ std::move(queue_.front()) };
            queue_.pop_front();
            return rc;
        }
        std::optional<T> try_pop()
        {
            auto lock{ std::scoped_lock(mutex_) };
            if (queue_.empty()) { return std::nullopt; }
            T rc{ std::move(queue_.front()) };
            queue_.pop_front();
            return rc;
        }
        void swap(ConcurrentQueue& other) noexcept(std::is_nothrow_swappable_v<Container> && noexcept(
            std::scoped_lock(std::declval<Mutex>())))
        {
            {
                auto lock{ std::scoped_lock(mutex_, other.mutex_) };
                queue_.swap(other.queue_);
            }
            condition_.notify_all();
            other.condition_.notify_all();
        }
        void resize(size_type count)
        {
            auto lock{ std::scoped_lock(mutex_) };
            queue_.resize(count);
        }
        void resize(size_type count, const value_type& value)
        {
            {
                auto lock{ std::scoped_lock(mutex_) };
                queue_.resize(count, value);
            }
            condition_.notify_all();
        }
        void clear() noexcept(std::is_nothrow_default_constructible_v<Container>&&
            std::is_nothrow_destructible_v<Container>&&
            std::is_nothrow_swappable_v<Container> && noexcept(
                std::scoped_lock(std::declval<Mutex>())))
        { /*https://devblogs.microsoft.com/oldnewthing/20201112-00/?p=104444 */
            Container trash{};
            auto lock{ std::scoped_lock(mutex_) };
            std::swap(trash, queue_);
        }
        [[nodiscard]] size_type clear_count() noexcept(
            std::is_nothrow_default_constructible_v<Container>&&
            std::is_nothrow_destructible_v<Container>&&
            std::is_nothrow_swappable_v<Container> && noexcept(
                std::declval<Container>()
                .size()) && noexcept(std::scoped_lock(std::declval<Mutex>())))
        {
            Container trash{};
            {
                auto lock{ std::scoped_lock(mutex_) };
                std::swap(trash, queue_);
            }
            return trash.size();
        }
        size_type clear_count_push(const T& value)
        {
            Container trash{};
            {
                auto lock{ std::scoped_lock(mutex_) };
                std::swap(trash, queue_);
                queue_.push_back(value);
            }
            condition_.notify_one();
            return trash.size();
        }
        size_type clear_count_push(T&& value)
        {
            Container trash{};
            {
                auto lock{ std::scoped_lock(mutex_) };
                std::swap(trash, queue_);
                queue_.push_back(std::move(value));
            }
            condition_.notify_one();
            return trash.size();
        }
        template<class... Args> auto clear_count_emplace(Args&&... args)
        {
            Container trash{};
            {
                T new_item{ std::forward<Args>(args)... };
                auto lock{ std::scoped_lock(mutex_) };
                std::swap(trash, queue_);
                queue_.push_back(std::move(new_item));
            }
            condition_.notify_one();
            return trash.size();
        }

    private:
        Container queue_{};
        mutable std::conditional_t<std::is_same_v<Mutex, std::mutex>, std::condition_variable,
            std::condition_variable_any>
            condition_{};
        mutable Mutex mutex_{};
    };
} // namespace rsj
#endif