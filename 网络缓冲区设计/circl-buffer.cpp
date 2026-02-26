#include <iostream>
#include <vector>
#include <algorithm>

class CircularBuffer {
public:
    CircularBuffer(std::size_t initial_size = 1024) : buffer_size(initial_size), head(0), tail(0) {
        buffer.resize(buffer_size);
    }

    // 写入数据
    void write(const char* data, std::size_t length) {
        if (length > available_space()) {
            resize_buffer(length);
        }

        if (tail + length <= buffer_size) {
            // 尾指针之后的空间足够写入数据
            std::copy(data, data + length, buffer.data() + tail);
            tail = (tail + length) % buffer_size;
        } else {
            // 尾指针之后的空间不足，需要拆分成两部分写入
            std::size_t space_to_end = buffer_size - tail;
            std::copy(data, data + space_to_end, buffer.data() + tail);
            std::copy(data + space_to_end, data + length, buffer.data());
            tail = length - space_to_end;
        }
    }

    // 读取数据
    std::size_t read(char* out_data, std::size_t length) {
        if (length > available_data()) {
            length = available_data();
        }

        if (head + length <= buffer_size) {
            // 头指针之后的空间足够读取数据
            std::copy(buffer.data() + head, buffer.data() + head + length, out_data);
            head = (head + length) % buffer_size;
        } else {
            // 头指针之后的空间不足，需要拆分成两部分读取
            std::size_t space_to_end = buffer_size - head;
            std::copy(buffer.data() + head, buffer.data() + buffer_size, out_data);
            std::copy(buffer.data(), buffer.data() + length - space_to_end, out_data + space_to_end);
            head = length - space_to_end;
        }

        return length;
    }

    // 获取可用数据长度
    std::size_t available_data() const {
        return (tail - head + buffer_size) % buffer_size;
    }

    // 获取可用空间长度
    std::size_t available_space() const {
        return (head - tail + buffer_size - 1) % buffer_size;
    }

private:
    std::vector<char> buffer;
    std::size_t buffer_size;
    std::size_t head; // 环形缓冲区的头指针
    std::size_t tail; // 环形缓冲区的尾指针

    // 扩容缓冲区
    void resize_buffer(std::size_t additional_length) {
        std::size_t new_size = std::max(buffer_size * 2, buffer_size + additional_length);
        std::vector<char> new_buffer(new_size);
        
        // 复制旧缓冲区的数据到新缓冲区
        std::size_t data_size = available_data();
        if (head < tail) {
            std::copy(buffer.data() + head, buffer.data() + tail, new_buffer.data());
        } else {
            std::size_t space_to_end = buffer_size - head;
            std::copy(buffer.data() + head, buffer.data() + buffer_size, new_buffer.data());
            std::copy(buffer.data(), buffer.data() + tail, new_buffer.data() + space_to_end);
        }

        buffer = std::move(new_buffer);
        buffer_size = new_size;
        head = 0;
        tail = data_size;
    }
};

int main() {
    CircularBuffer buffer;
    char write_data1[] = "Hello, ";
    char write_data2[] = "Fitten Code!";
    buffer.write(write_data1, sizeof(write_data1) - 1); // 写入数据，不包括字符串结尾的'\0'
    buffer.write(write_data2, sizeof(write_data2) - 1); // 写入数据，不包括字符串结尾的'\0'

    char read_data[50];
    std::size_t read_length = buffer.read(read_data, 5); // 读取5个字符
    read_data[read_length] = '\0'; // 添加字符串结尾
    std::cout << "Read data: " << read_data << std::endl;

    read_length = buffer.read(read_data, 20); // 继续读取剩余数据
    read_data[read_length] = '\0'; // 添加字符串结尾
    std::cout << "Read data: " << read_data << std::endl;

    return 0;
}
