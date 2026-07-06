#pragma once
#include <vector>
#include <cstring>
#include <algorithm>

// 원형 큐 기반의 링버퍼 클래스 (RingBuffer)
// 선형 버퍼의 memmove/memcpy 오버헤드를 제거하기 위해 도입됨.
class RingBuffer {
private:
    std::vector<char> _buffer;
    int _capacity;
    int _head;
    int _tail;

public:
    // 빈 상태와 가득 찬 상태를 구분하기 위해 capacity + 1 크기로 할당
    RingBuffer(int capacity = 8192) : _capacity(capacity), _head(0), _tail(0) {
        _buffer.resize(capacity + 1); 
    }

    void Clear() {
        _head = _tail = 0;
    }

    // 버퍼의 남은 전체 공간 크기
    int GetFreeCapacity() const {
        if (_tail >= _head) {
            return _capacity - (_tail - _head);
        }
        return _head - _tail - 1;
    }

    // 현재 기록되어 있는 데이터의 전체 크기
    int GetStoredSize() const {
        if (_tail >= _head) {
            return _tail - _head;
        }
        return _capacity + 1 - (_head - _tail);
    }

    // 현재 Tail에서부터 연속으로 기록할 수 있는 공간 크기 (끝단 도달 시 끊김)
    int GetDirectWriteSize() const {
        if (_tail >= _head) {
            return _capacity + 1 - _tail - (_head == 0 ? 1 : 0);
        }
        return _head - _tail - 1;
    }

    // WSARecv 등을 위해 기록할 메모리 주소 반환
    char* GetWritePos() {
        return &_buffer[_tail];
    }

    // 기록 완료 후 Tail 전진
    void CommitWrite(int size) {
        _tail = (_tail + size) % (_capacity + 1);
    }

    // 현재 Head에서부터 연속으로 읽을 수 있는 데이터 크기
    int GetDirectReadSize() const {
        if (_tail >= _head) {
            return _tail - _head;
        }
        return _capacity + 1 - _head;
    }

    char* GetReadPos() {
        return &_buffer[_head];
    }

    // 읽기 완료 후 Head 전진
    void CommitRead(int size) {
        _head = (_head + size) % (_capacity + 1);
    }

    // 랩어라운드(Wrap-around)를 처리하며 원하는 크기만큼 대상 버퍼로 패킷을 엿봄(포인터 이동 X)
    bool Peek(char* dest, int size) {
        if (GetStoredSize() < size) return false;

        int directReadSize = GetDirectReadSize();
        if (size <= directReadSize) {
            std::memcpy(dest, &_buffer[_head], size);
        } else {
            std::memcpy(dest, &_buffer[_head], directReadSize);
            std::memcpy(dest + directReadSize, &_buffer[0], size - directReadSize);
        }
        return true;
    }

    // 랩어라운드를 처리하며 패킷을 읽어옴 (Read + CommitRead)
    bool Read(char* dest, int size) {
        if (Peek(dest, size)) {
            CommitRead(size);
            return true;
        }
        return false;
    }
};
