
#pragma once

#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;
 
    explicit RawMemory(size_t capacity) : buffer_(Allocate(capacity))
                                        , capacity_(capacity) {}
 
    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    
    RawMemory(RawMemory&& other) noexcept 
        : buffer_(std::exchange(other.buffer_, nullptr))
        , capacity_(std::exchange(other.capacity_, 0)) {}
 
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            buffer_ = nullptr;
            capacity_ = 0;
            Swap(rhs);
        }
        return *this;
    }
    
    ~RawMemory() {Deallocate(buffer_);}
 
    T* operator+(size_t offset) noexcept {assert(offset <= capacity_); return buffer_ + offset;}
    const T* operator+(size_t offset) const noexcept {return const_cast<RawMemory&>(*this) + offset;}
 
    const T& operator[](size_t index) const noexcept {return const_cast<RawMemory&>(*this)[index];}
    T& operator[](size_t index) noexcept {assert(index < capacity_); return buffer_[index];}
 
    void Swap(RawMemory& other) noexcept {std::swap(buffer_, other.buffer_); std::swap(capacity_, other.capacity_);}
 
    const T* GetAddress() const noexcept {return buffer_;}
    T* GetAddress() noexcept {return buffer_;}
    size_t Capacity() const {return capacity_;}
 
private:
    T* buffer_ = nullptr;
    size_t capacity_ = 0;
    
    static T* Allocate(size_t n) {return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;}
    static void Deallocate(T* buf) noexcept {operator delete(buf);}
}; 

template <typename T>
class Vector {
public:

    using iterator = T*;
    using const_iterator = const T*;
    
    iterator begin() noexcept { return data_.GetAddress(); }
    iterator end() noexcept { return size_ + data_.GetAddress(); }
    const_iterator cbegin() const noexcept { return data_.GetAddress(); }
    const_iterator cend() const noexcept { return size_ + data_.GetAddress(); }    
    const_iterator begin() const noexcept { return cbegin(); }
    const_iterator end() const noexcept { return cend(); }
    
    Vector() = default;
    
    explicit Vector(size_t size) 
        : data_(size)
        , size_(size) {
        std::uninitialized_value_construct_n(begin(), size);
    }
    
    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, begin());
    }
    
    Vector(Vector&& other) noexcept 
        : data_(std::move(other.data_))
        , size_(std::exchange(other.size_, 0)) {}
        
    void CopyNotSwap(const Vector& src) {
        const T* src_begin = src.data_.GetAddress();
        std::copy(src_begin, src_begin + std::min(size_, src.size_), begin());
        if (size_ <= src.size_) {
            std::uninitialized_copy_n(src_begin + size_, src.size_ - size_, end());
        } 
        else {
            std::destroy_n(begin() + src.size_, size_ - src.size_);
        }
        size_ = src.size_;        
    }
        
    Vector& operator=(const Vector& other) {
        if (this == &other) return *this;
        if (other.size_ <= Capacity()) {
            CopyNotSwap(other);
        } 
        else {              
            Vector other_copy(other);
            Swap(other_copy);
        }
        return *this;
    }
    
    Vector& operator=(Vector&& other) noexcept {
        if(this != &other) {    
            Swap(other);
        }
        return *this;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) { return; }
        RawMemory<T> new_data(new_capacity);
        T* new_begin = new_data.GetAddress();
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin(), size_, new_begin);
        } else {
            std::uninitialized_copy_n(begin(), size_, new_begin);
        }
        data_.Swap(new_data);
        std::destroy_n(new_begin, size_);
    }
    
    void Resize(size_t new_size) {
        new_size < size_ ? (void)(std::destroy_n(begin() + new_size, size_ - new_size)) : void();
        if (new_size > size_) { 
            new_size > data_.Capacity() ? (void)(Reserve(std::max(data_.Capacity() * 2, new_size))) : void();
            std::uninitialized_value_construct_n(end(), new_size - size_);
        }
        size_ = new_size;
    }
    
    void PopBack() {
        assert(size_);
        std::destroy_at(begin() + --size_);
    }    
    
    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ < Capacity()) {
            new (data_.GetAddress() + size_) T(std::forward<Args>(args)...);
            ++size_;
            return data_[size_-1];
        }
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        T* new_begin = new_data.GetAddress();
        new (new_begin + size_) T(std::forward<Args>(args)...);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin(), size_, new_begin);
        } 
        else {
            std::uninitialized_copy_n(begin(), size_, new_begin);
        }
        std::destroy_n(begin(), size_++);
        data_.Swap(new_data);
        return data_[size_-1];
    }
    
    template <typename S>
    void PushBack(S&& value) {
        EmplaceBack(std::forward<S>(value));
    }
    
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        if (pos < begin() && pos > end()) return nullptr;
        int new_pos = pos - begin(); 
        if (size_ < data_.Capacity()) {
            T t(std::forward<Args>(args)...);
            if (pos != end()) {
                new (end()) T(std::forward<T>(data_[size_- 1])); 
                std::move_backward(begin() + new_pos, end() - 1, end());
            }
            *(begin() + new_pos) = std::forward<T>(t);
            ++size_;
            return begin() + new_pos;
        }
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        T* new_begin = new_data.GetAddress();
        new (new_begin + new_pos) T(std::forward<Args>(args)...);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin(), new_pos, new_begin);
            std::uninitialized_move_n(begin() + new_pos, size_ - new_pos, new_begin + new_pos + 1);
        } 
        else {
            std::uninitialized_copy_n(begin(), new_pos - 1, new_begin);
            std::uninitialized_copy_n(begin() + new_pos, size_ - new_pos, new_begin + new_pos + 1);
        }
        std::destroy_n(begin(), size_++);
        data_.Swap(new_data);
        return begin() + new_pos;
    }
    
    iterator Erase(const_iterator pos) {
        if (pos < begin() && pos >= end()) return nullptr;
        int new_pos = pos - begin();
        std::move(begin() + new_pos + 1, end(), begin() + new_pos);
        std::destroy_at(end() - 1);
        --size_;
        return begin() + new_pos;
    }
    
    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }  

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_), std::swap(size_, other.size_);
    }
    
    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }
    
    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);;
    }

private:
    RawMemory<T>  data_;
    size_t size_ = 0;
};
