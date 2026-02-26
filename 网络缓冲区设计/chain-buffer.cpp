#include <iostream>
#include <vector>
#include <memory>

class ChainBuffer {
public:
    ChainBuffer(std::size_t initial_size = 1024) : initial_size(initial_size), first(nullptr), last(nullptr) {
        Node* newNode = new Node(initial_size);
        first = newNode;
        last = newNode;
    }

    ~ChainBuffer() {
        Node* current = first;
        while (current != nullptr) {
            Node* next = current->next;
            delete current;
            current = next;
        }
    }

    // 写入数据
    void write(const char* data, std::size_t length) {
        while (length > 0) {
            if (last->available_space() == 0) {
                // 当前节点空间不足，新增一个节点
                Node* newNode = new Node(last->size * 2); // 新节点大小为当前节点的两倍
                last->next = newNode;
                last = newNode;
            }

            std::size_t space_to_end = last->available_space();
            std::size_t write_length = std::min(length, space_to_end);

            std::copy(data, data + write_length, last->data.data() + last->tail);
            last->tail = (last->tail + write_length) % last->size;
            data += write_length;
            length -= write_length;
        }
    }

    // 读取数据
    std::size_t read(char* out_data, std::size_t length) {
        std::size_t total_read = 0;
        while (total_read < length && first != nullptr) {
            std::size_t available_data = first->available_data();
            if (available_data == 0) {
                // 当前节点没有可读数据，删除当前节点并移动到下一个节点
                Node* next = first->next;
                delete first;
                first = next;
                if (first == nullptr) {
                    last = nullptr;
                }
                continue;
            }

            std::size_t read_length = std::min(length - total_read, available_data);
            std::size_t space_to_end = first->size - first->head;

            if (read_length <= space_to_end) {
                std::copy(first->data.data() + first->head, first->data.data() + first->head + read_length, out_data + total_read);
                first->head = (first->head + read_length) % first->size;
            } else {
                std::copy(first->data.data() + first->head, first->data.data() + first->size, out_data + total_read);
                std::size_t remaining_read = read_length - space_to_end;
                std::copy(first->data.data(), first->data.data() + remaining_read, out_data + total_read + space_to_end);
                first->head = remaining_read;
            }

            total_read += read_length;
        }

        // 如果已经读完的数据节点，清除它
        if (first != nullptr && first->available_data() == 0) {
            Node* next = first->next;
            delete first;
            first = next;
            if (first == nullptr) {
                last = nullptr;
            }
        }

        return total_read;
    }

private:
    struct Node {
        std::vector<char> data;
        std::size_t size;
        int head;
        int tail;
        Node* next;

        Node(std::size_t size) : size(size), head(0), tail(0), next(nullptr) {
            data.resize(size);
        }

        // 获取可用数据长度
        std::size_t available_data() const {
            return (tail - head + size) % size;
        }

        // 获取可用空间长度
        std::size_t available_space() const {
            return (head - tail + size - 1) % size;
        }
    };

    std::size_t initial_size;
    Node* first;
    Node* last;

    // 获取可用空间长度
    std::size_t available_space() {
        std::size_t space = 0;
        Node* current = last;
        while (current != nullptr) {
            space += current->available_space();
            current = current->next;
        }
        return space;
    }

    // 获取可用数据长度
    std::size_t available_data() {
        std::size_t data = 0;
        Node* current = first;
        while (current != nullptr) {
            data += current->available_data();
            current = current->next;
        }
        return data;
    }
};

int main() {
    ChainBuffer buffer;
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
