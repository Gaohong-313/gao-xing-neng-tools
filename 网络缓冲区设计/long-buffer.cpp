#include <iostream>
#include <vector>
#include <algorithm>

class DynamicBuffer {
public:
    DynamicBuffer(std::size_t initial_size = 1024) : buffer_size(initial_size), read_pos(0), write_pos(0) {
        buffer.resize(buffer_size);
    }

    // 写入数据
    void write(const char* data, std::size_t length) {
        if (length > available_space()) {
            resize_buffer(length);
        }
        std::copy(data, data + length, buffer.data() + write_pos);
        write_pos += length;
    }

    // 读取数据
    std::size_t read(char* out_data, std::size_t length) {
        if (length > available_data()) {
            length = available_data();
        }
        std::copy(buffer.data() + read_pos, buffer.data() + read_pos + length, out_data);
        move_remaining_data();
        return length;
    }

    // 获取可用数据长度
    std::size_t available_data() const {
        return write_pos - read_pos;
    }

private:
    std::vector<char> buffer;
    std::size_t buffer_size;
    std::size_t read_pos;
    std::size_t write_pos;

    // 获取可用空间长度
    std::size_t available_space() const {
        return buffer_size - write_pos;
    }

    // 扩容缓冲区
    void resize_buffer(std::size_t additional_length) {
        buffer_size = std::max(buffer_size * 2, buffer_size + additional_length);
        buffer.resize(buffer_size);
    }

    // 移动未读取的数据到缓冲区前部
    void move_remaining_data() {
        if (read_pos > 0) {
            std::move(buffer.data() + read_pos, buffer.data() + write_pos, buffer.data());
            write_pos -= read_pos;
            read_pos = 0;
        }
    }
};

int main() {
    DynamicBuffer buffer;
    char write_data[] = "Hello, Fitten Code!";
    buffer.write(write_data, sizeof(write_data) - 1); // 写入数据，不包括字符串结尾的'\0'

    char read_data[50];
    std::size_t read_length = buffer.read(read_data, 5); // 读取5个字符
    read_data[read_length] = '\0'; // 添加字符串结尾
    std::cout << "Read data: " << read_data << std::endl;

    read_length = buffer.read(read_data, 20); // 继续读取剩余数据
    read_data[read_length] = '\0'; // 添加字符串结尾
    std::cout << "Read data: " << read_data << std::endl;

    return 0;
}
