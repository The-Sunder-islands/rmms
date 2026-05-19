#include "transport/shared_mem.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rmms::backend::transport {

std::unique_ptr<SharedMemoryPool> SharedMemoryPool::create(std::string_view name, size_t size) {
    std::string full_name = "/rmms_";
    full_name += name;

    int fd = shm_open(full_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) {
        throw std::runtime_error(
            std::string("shm_open(CREATE) '") + full_name + "' failed: " + strerror(errno));
    }

    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        shm_unlink(full_name.c_str());
        throw std::runtime_error(
            std::string("ftruncate() '") + full_name + "' failed: " + strerror(errno));
    }

    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        close(fd);
        shm_unlink(full_name.c_str());
        throw std::runtime_error(
            std::string("mmap() '") + full_name + "' failed: " + strerror(errno));
    }

    return std::unique_ptr<SharedMemoryPool>(
        new SharedMemoryPool(std::move(full_name), fd, ptr, size, true));
}

std::unique_ptr<SharedMemoryPool> SharedMemoryPool::open(std::string_view name) {
    std::string full_name = "/rmms_";
    full_name += name;

    int fd = shm_open(full_name.c_str(), O_RDWR, 0600);
    if (fd < 0) {
        throw std::runtime_error(
            std::string("shm_open(OPEN) '") + full_name + "' failed: " + strerror(errno));
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        throw std::runtime_error(
            std::string("fstat() '") + full_name + "' failed: " + strerror(errno));
    }

    void* ptr = mmap(nullptr, static_cast<size_t>(st.st_size),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error(
            std::string("mmap() '") + full_name + "' failed: " + strerror(errno));
    }

    return std::unique_ptr<SharedMemoryPool>(
        new SharedMemoryPool(std::move(full_name), fd, ptr,
                             static_cast<size_t>(st.st_size), false));
}

SharedMemoryPool::SharedMemoryPool(std::string name, int fd, void* ptr, size_t sz, bool owner)
    : m_name(std::move(name))
    , m_fd(fd)
    , m_ptr(ptr)
    , m_size(sz)
    , m_owner(owner)
{
}

SharedMemoryPool::~SharedMemoryPool() {
    if (m_ptr && m_ptr != MAP_FAILED)
        munmap(m_ptr, m_size);
    if (m_fd >= 0)
        close(m_fd);
    if (m_owner)
        shm_unlink(m_name.c_str());
}

SharedMemoryPool::SharedMemoryPool(SharedMemoryPool&& other) noexcept
    : m_name(std::move(other.m_name))
    , m_fd(other.m_fd)
    , m_ptr(other.m_ptr)
    , m_size(other.m_size)
    , m_owner(other.m_owner)
{
    other.m_fd = -1;
    other.m_ptr = nullptr;
    other.m_size = 0;
    other.m_owner = false;
}

SharedMemoryPool& SharedMemoryPool::operator=(SharedMemoryPool&& other) noexcept {
    if (this != &other) {
        if (m_ptr && m_ptr != MAP_FAILED)
            munmap(m_ptr, m_size);
        if (m_fd >= 0)
            close(m_fd);
        if (m_owner)
            shm_unlink(m_name.c_str());

        m_name = std::move(other.m_name);
        m_fd = other.m_fd;
        m_ptr = other.m_ptr;
        m_size = other.m_size;
        m_owner = other.m_owner;

        other.m_fd = -1;
        other.m_ptr = nullptr;
        other.m_size = 0;
        other.m_owner = false;
    }
    return *this;
}

void* SharedMemoryPool::data() const {
    return m_ptr;
}

size_t SharedMemoryPool::size() const {
    return m_size;
}

std::string_view SharedMemoryPool::name() const {
    return m_name;
}

}  // namespace rmms::backend::transport
