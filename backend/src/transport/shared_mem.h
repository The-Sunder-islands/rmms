#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace rmms::backend::transport {

class SharedMemoryPool {
public:
    static std::unique_ptr<SharedMemoryPool> create(std::string_view name, size_t size);

    static std::unique_ptr<SharedMemoryPool> open(std::string_view name);

    ~SharedMemoryPool();

    SharedMemoryPool(const SharedMemoryPool&) = delete;
    SharedMemoryPool& operator=(const SharedMemoryPool&) = delete;
    SharedMemoryPool(SharedMemoryPool&&) noexcept;
    SharedMemoryPool& operator=(SharedMemoryPool&&) noexcept;

    void* data() const;
    size_t size() const;
    std::string_view name() const;

private:
    SharedMemoryPool(std::string name, int fd, void* ptr, size_t sz, bool owner);

    std::string m_name;
    int         m_fd;
    void*       m_ptr;
    size_t      m_size;
    bool        m_owner;
};

}  // namespace rmms::backend::transport
