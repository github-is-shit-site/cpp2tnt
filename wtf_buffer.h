#ifndef WTF_BUFFER_H
#define WTF_BUFFER_H

#include <vector>
#include <functional>

/// lazy buffer (my bad :)
class wtf_buffer
{
public:
    wtf_buffer(size_t size = 1024 * 1024);
    size_t capacity() const noexcept;
    size_t size() const noexcept;
    size_t available() const noexcept;
    char* data() noexcept;
    void reserve(size_t size);
    void resize(size_t size);
    void clear() noexcept;
    void swap(wtf_buffer &other) noexcept;
    char *end; // let msgpuck to write directly
    std::function<void()> on_clear;
private:
    std::vector<char> _buf;
};

#endif // WTF_BUFFER_H
