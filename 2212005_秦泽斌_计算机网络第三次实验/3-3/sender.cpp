#include <winsock2.h>
#include <Ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
#include <windows.h>
#include <time.h>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
using namespace std;

#define WIN32_LEAN_AND_MEAN
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 27000
#define DEFAULT_BUFLEN 1024
#define DEFAULT_SEQNUM 65536
#define UDP_LEN sizeof(my_udp)
#define MAX_FILESIZE 1024 * 1024 * 10
#define MAX_TIME  0.2 * CLOCKS_PER_SEC // 超时重传

// 连接上ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

const uint16_t SYN = 0x1; // SYN = 1 ACK = 0 FIN = 0
const uint16_t ACK = 0x2; // SYN = 0, ACK = 1，FIN = 0
const uint16_t SYN_ACK = 0x3; // SYN = 1, ACK = 1
const uint16_t START = 0x10;
const uint16_t OVER = 0x8;
const uint16_t FIN = 0x4; // FIN = 1 ACK = 0
const uint16_t FIN_ACK = 0x6; // FIN = 1 ACK = 1
const uint16_t START_OVER = 0x18; // START = 1 OVER = 1

uint16_t seq_order = 0;
uint16_t stream_seq_order = 0;
long file_size = 0;
//为字体设置颜色
void setConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

struct HEADER {
    uint16_t datasize;
    uint16_t cksum;
    uint16_t Flag;
    uint16_t STREAM_SEQ;
    uint16_t SEQ;

    // 初始化函数，STREAM标记的是文件的开始与结束
    // 开始的时候需要把文件名传输过去，所以给两位标志位分别为START OVER

    HEADER() {
        this->datasize = 0;
        this->cksum = 0;
        this->Flag = 0;
        this->STREAM_SEQ = 0;
        this->SEQ = 0;
    }

    HEADER(uint16_t datasize, uint16_t cksum, uint16_t Flag, uint16_t STREAM_SEQ, uint16_t SEQ) {
        this->datasize = datasize;
        this->cksum = cksum;
        this->Flag = Flag;
        this->STREAM_SEQ = STREAM_SEQ;
        this->SEQ = SEQ;
    }

    void set_value(uint16_t datasize, uint16_t cksum, uint16_t Flag, uint16_t STREAM_SEQ, uint16_t SEQ) {
        this->datasize = datasize;
        this->cksum = cksum;
        this->Flag = Flag;
        this->STREAM_SEQ = STREAM_SEQ;
        this->SEQ = SEQ;
    }
};

class my_udp {
public:
    HEADER udp_header;
    char buffer[DEFAULT_BUFLEN] = "";
public:
    my_udp() {};
    my_udp(HEADER& header);
    my_udp(HEADER& header, string data_segment);
    void set_value(HEADER header, char* data_segment, int size);
};

// 针对三次握手和四次挥手的初始化函数
my_udp::my_udp(HEADER& header) {
    udp_header = header;
};

my_udp::my_udp(HEADER& header, string data_segment) {
    udp_header = header;
    for (int i = 0; i < data_segment.length(); i++) {
        buffer[i] = data_segment[i];
    }
    buffer[data_segment.length()] = '\0';
};

void my_udp::set_value(HEADER header, char* data_segment, int size) {
    udp_header = header;
    memcpy(buffer, data_segment, size);
}

// 计算校验和
uint16_t checksum(uint16_t* udp, int size) {
    int count = (size + 1) / 2;
    uint16_t* buf = (uint16_t*)malloc(size);
    memset(buf, 0, size);
    memcpy(buf, udp, size);
    u_long sum = 0;
    while (count--) {
        sum += *buf++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
    return ~(sum & 0xffff);
}

// 检验全局变量的序列号有没有超过最大值
void check_seq() {
    if (seq_order >= DEFAULT_SEQNUM) {
        seq_order = seq_order % DEFAULT_SEQNUM;
    }
}

void check_stream_seq() {
    if (stream_seq_order >= DEFAULT_SEQNUM) {
        stream_seq_order = stream_seq_order % DEFAULT_SEQNUM;
    }
}
void print_Send_information(my_udp& udp2show, string statue) {
    setConsoleColor(7);
    cout << "已发送数据包" << udp2show.udp_header.SEQ << "!   数据包大小为 " << udp2show.udp_header.datasize << " bytes" << endl;
    cout << "校验和:" << udp2show.udp_header.cksum << "     ACK:" << udp2show.udp_header.Flag << endl;
}

void send_packet_GBN(my_udp& Packet, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    int iResult;
    int RecvAddrSize = sizeof(RecvAddr);
    char* SendBuf = new char[UDP_LEN];

    memcpy(SendBuf, &Packet, UDP_LEN);
    iResult = sendto(SendSocket, SendBuf, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "Sendto failed with error: " << WSAGetLastError() << endl;
    }

    delete[] SendBuf;
}
void send_packet(my_udp& Packet, SOCKET& SendSocket, sockaddr_in& RecvAddr, int packet_num) {
    int iResult;
    int RecvAddrSize = sizeof(RecvAddr);
    my_udp Recv_udp;
    char* SendBuf = new char[UDP_LEN];
    char* RecvBuf = new char[UDP_LEN];
    memcpy(SendBuf, &Packet, UDP_LEN);
    iResult = sendto(SendSocket, SendBuf, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "Sendto failed with error: " << WSAGetLastError() << endl;
    }

    print_Send_information(Packet, "Send");

    // 记录发送时间，超时重传
    // 等待接收ACK信息，验证序列号
    clock_t start = clock();
    while (true) {
        while (recvfrom(SendSocket, RecvBuf, UDP_LEN, 0, (sockaddr*)&RecvAddr, &RecvAddrSize) <= 0) {

            if (clock() - start > MAX_TIME) {
                setConsoleColor(12);
                cout << "#####传输超时！即将重新发送数据包 " << Packet.udp_header.SEQ << "!#####" << endl;


                Packet.udp_header.SEQ = seq_order;
                memcpy(SendBuf, &Packet, UDP_LEN);
                iResult = sendto(SendSocket, SendBuf, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
                print_Send_information(Packet, "ReSend");
                start = clock(); // 重设开始时间
                if (iResult == SOCKET_ERROR) {
                    setConsoleColor(12);
                    cout << "Sendto failed with error: " << WSAGetLastError() << endl;

                }
            }
        }

        memcpy(&Recv_udp, RecvBuf, UDP_LEN);
        if (Recv_udp.udp_header.SEQ == Packet.udp_header.SEQ && Recv_udp.udp_header.Flag == ACK && checksum((uint16_t*)&Recv_udp, UDP_LEN) == 0) {
            setConsoleColor(7);
            cout << "数据包已确认被接受！    " << " 当前发送进度:" << Packet.udp_header.SEQ << "/" << packet_num << "   ACK:" << ACK << endl << endl;
            seq_order++; // 全局变量的序列号
            check_seq();
            break;
        }
        else {
            continue;
        }
    }

    delete[] SendBuf;
    delete[] RecvBuf;
}

// GBN部分的全局变量
const int N = 32; // 窗口大小
uint16_t base = 1; // 初始为1
uint16_t next_seqnum = 1;
int ACK_index = 0; // 判断结束
queue<my_udp> message_queue;
//Reno部分的全局变量
int cwnd = 1;//拥塞窗口
int ssthresh = 16; // 慢启动阈值
int duplicate_ACK_count = 0; // 重复ACK计数
int RTT_ACK = 0;
// 接收到ACK时需要更新的共享数据
mutex mtx;
condition_variable cv;
bool ack_received = false;

void GBN_init() {
    base = 1;
    next_seqnum = 1;
    ACK_index = 0;
}
void Reno_init() {
    base = 1;
    next_seqnum = 1;
    ACK_index = 0;
    cwnd = 1;
    ssthresh =16;
    duplicate_ACK_count = 0;
}

//拥塞控制代码
//接收函数的线程
void receive_ACK_thread_Reno(SOCKET& SendSocket, sockaddr_in& RecvAddr, uint16_t& base, int& ACK_index, queue<my_udp>& message_queue, int& packet_num, clock_t& start) {
    char RecvBuf[UDP_LEN];
    my_udp Recv_udp;
    int RecvAddrSize = sizeof(RecvAddr);
    int FastN = 0;
    int FastStatu = 0;


    while (ACK_index < packet_num) {  // 持续接收 ACK，直到所有数据包被确认
        if (recvfrom(SendSocket, RecvBuf, UDP_LEN, 0, (sockaddr*)&RecvAddr, &RecvAddrSize) > 0) {
            memcpy(&Recv_udp, RecvBuf, UDP_LEN);

            // 检查是否是有效的 ACK 包
            if (Recv_udp.udp_header.Flag == ACK && checksum((uint16_t*)&Recv_udp, UDP_LEN) == 0) {
                lock_guard<mutex> lock(mtx);  // 保护 base 和队列的访问
                if ((base % DEFAULT_SEQNUM) == Recv_udp.udp_header.SEQ) {
                    // 弹出队列中已确认的数据包
                    if (!message_queue.empty()) {
                        message_queue.pop();
                    }
                    start = clock();//重置计时器
                    base++;  // 滑动窗口前进
                    ACK_index++;
                    setConsoleColor(7);
                    cout << "[ACK RECEIVED] base=" << base << ", ACK_index=" << ACK_index << endl;

                    //收到正确的ACK，分为慢启动阶段和拥塞避免阶段两种情况
                    FastStatu = 0;//恢复快速重传为可用状态
                    FastN = 0;
                    duplicate_ACK_count = 0;//重置重复ACK计数
                    if (cwnd < ssthresh) {
                        cwnd++;//慢启动阶段，每收到一个正确ACK，窗口+1
                        RTT_ACK = 0;
                        setConsoleColor(1);
                        cout << "慢启动阶段，每收到一个正确ACK，窗口+1, cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;
                    }
                    else {
                        RTT_ACK++;
                        if (RTT_ACK == cwnd) {
                            cwnd++;//拥塞避免阶段，每收到一个窗口的正确ACK，窗口+1
                            RTT_ACK = 0;
                            setConsoleColor(1);
                            cout << "拥塞避免阶段，每收到一个窗口的正确ACK，窗口+1, cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;
                        }
                    }

                }
                else {//收到了非预期的ACK包
                    setConsoleColor(12);
                    cout << "[DUPLICATE ACK] SEQ=" << Recv_udp.udp_header.SEQ << endl;
                    duplicate_ACK_count++;
                    //连续收到三次重复ACK包，触发快速重传
                    if (duplicate_ACK_count == 3&& FastStatu == 0) {
                        FastN++;
                        duplicate_ACK_count = 0;
                        RTT_ACK = 0;
                        ssthresh = cwnd / 2;
                        cwnd = ssthresh + 3;
                        setConsoleColor(1);
                        cout << "快速恢复阶段，cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;
                        start = clock();//重置计时器
                        queue<my_udp> temp_queue = message_queue;  // 创建副本用于重发
                        while (!temp_queue.empty()) {
                            send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                            setConsoleColor(7);
                            cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                            temp_queue.pop();
                        }
                    }
                    if (FastN == 2) {
                        FastN = 0;
                        FastStatu = 1;//避免连续快速重传
                        setConsoleColor(12);
                        cout << "[TIMEOUT] 重发未确认数据包." << endl;
                        start = clock();
                        //超时重传，重新回到慢启动状态,阈值减半
                        RTT_ACK = 0;
                        ssthresh = cwnd / 2;
                        cwnd = 1;
                        setConsoleColor(1);
                        cout << "重新进入慢启动阶段，cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;


                        queue<my_udp> temp_queue = message_queue;  // 创建副本用于重发
                        while (!temp_queue.empty()) {
                            send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                            setConsoleColor(7);
                            cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                            temp_queue.pop();
                        }
                    }
                }
            }
        }
    }
}


// Reno的发送文件函数
void send_file_Reno(string filename, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    Reno_init();
    int RecvAddrSize = sizeof(RecvAddr);

    // 读取文件数据
    ifstream fin(filename.c_str(), ifstream::binary);
    fin.seekg(0, std::ifstream::end);
    long size = fin.tellg();
    file_size = size;
    fin.seekg(0);

    char* binary_file_buf = new char[size];
    setConsoleColor(1);
    cout << " #####文件大小：" << size << " bytes" << endl;
    fin.read(&binary_file_buf[0], size);
    fin.close();

    HEADER udp_header(filename.length(), 0, START, stream_seq_order, 0);
    my_udp udp_packets(udp_header, filename.c_str());
    uint16_t check = checksum((uint16_t*)&udp_packets, UDP_LEN); // 计算校验和
    udp_packets.udp_header.cksum = check;

    int packet_num = size / DEFAULT_BUFLEN + 1;
    setConsoleColor(1);
    cout << " #####文件名校验和：" << check << endl;
    cout << " #####送数据包的数量：" << packet_num << endl;
    cout << " #####滑动窗口大小：" << cwnd << endl;

    // 正常发送第一个文件名数据包
    send_packet(udp_packets, SendSocket, RecvAddr, packet_num);
    //创建计时器 
    clock_t start = clock();
    // 创建接收 ACK 的线程
    thread ack_thread(receive_ACK_thread_Reno, ref(SendSocket), ref(RecvAddr), ref(base), ref(ACK_index), ref(message_queue), ref(packet_num), ref(start));

    // 主线程发送数据包
    while (ACK_index < packet_num) {
        lock_guard<mutex> lock(mtx);  // 确保共享变量的安全访问

        // 检查是否可以发送新的数据包
        while (next_seqnum < base + cwnd && next_seqnum <= packet_num) {
            uint16_t remainder = next_seqnum % DEFAULT_SEQNUM;
            if (next_seqnum == packet_num) {  // 最后一个包
                udp_header.set_value(size - (next_seqnum - 1) * DEFAULT_BUFLEN, 0, OVER, stream_seq_order, remainder);
                udp_packets.set_value(udp_header, binary_file_buf + (next_seqnum - 1) * DEFAULT_BUFLEN, size - (next_seqnum - 1) * DEFAULT_BUFLEN);
            }
            else {  // 正常包
                udp_header.set_value(DEFAULT_BUFLEN, 0, 0, stream_seq_order, remainder);
                udp_packets.set_value(udp_header, binary_file_buf + (next_seqnum - 1) * DEFAULT_BUFLEN, DEFAULT_BUFLEN);
            }

            udp_packets.udp_header.cksum = checksum((uint16_t*)&udp_packets, UDP_LEN);
            send_packet_GBN(udp_packets, SendSocket, RecvAddr);
            setConsoleColor(7);
            cout << "[SEND] Packet SEQ=" << remainder << " Checksum = " << udp_packets.udp_header.cksum << endl;
            message_queue.push(udp_packets);
            next_seqnum++;
        }

        // 检查超时并重发
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "[TIMEOUT] 重发未确认数据包." << endl;
            start = clock();
            //超时重传，重新回到慢启动状态,阈值减半
            RTT_ACK = 0;
            ssthresh = cwnd / 2;
            cwnd = 1;
            setConsoleColor(1);
            cout << "重新进入慢启动阶段，cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;


            queue<my_udp> temp_queue = message_queue;  // 创建副本用于重发
            while (!temp_queue.empty()) {
                send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                setConsoleColor(7);
                cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                temp_queue.pop();
            }
        }
    }

    // 等待接收线程结束
    ack_thread.join();
    setConsoleColor(10);
    cout << "#####文件传输完成！#####" << endl << endl;
    stream_seq_order++;
    check_stream_seq();
    delete[] binary_file_buf;
}

//接收函数的线程
void receive_ACK_thread(SOCKET& SendSocket, sockaddr_in& RecvAddr, uint16_t& base, int& ACK_index, queue<my_udp>& message_queue, int& packet_num, clock_t& start) {
    char RecvBuf[UDP_LEN];
    my_udp Recv_udp;
    int RecvAddrSize = sizeof(RecvAddr);

    while (ACK_index < packet_num) {  // 持续接收 ACK，直到所有数据包被确认
        if (recvfrom(SendSocket, RecvBuf, UDP_LEN, 0, (sockaddr*)&RecvAddr, &RecvAddrSize) > 0) {
            memcpy(&Recv_udp, RecvBuf, UDP_LEN);

            // 检查是否是有效的 ACK 包
            if (Recv_udp.udp_header.Flag == ACK && checksum((uint16_t*)&Recv_udp, UDP_LEN) == 0) {
                lock_guard<mutex> lock(mtx);  // 保护 base 和队列的访问
                if ((base % DEFAULT_SEQNUM) == Recv_udp.udp_header.SEQ) {
                    // 弹出队列中已确认的数据包
                    if (!message_queue.empty()) {
                        message_queue.pop();
                    }
                    start = clock();//重置计时器
                    base++;  // 滑动窗口前进
                    ACK_index++;
                    setConsoleColor(7);
                    cout << "[ACK RECEIVED] base=" << base << ", ACK_index=" << ACK_index << endl;


                }
                else {
                    setConsoleColor(12);
                    cout << "[DUPLICATE ACK] SEQ=" << Recv_udp.udp_header.SEQ << endl;
                }
            }
        }
    }
}


// GBN的发送文件函数
void send_file_GBN(string filename, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    GBN_init();
    int RecvAddrSize = sizeof(RecvAddr);

    // 读取文件数据
    ifstream fin(filename.c_str(), ifstream::binary);
    fin.seekg(0, std::ifstream::end);
    long size = fin.tellg();
    file_size = size;
    fin.seekg(0);

    char* binary_file_buf = new char[size];
    setConsoleColor(1);
    cout << " #####文件大小：" << size << " bytes" << endl;
    fin.read(&binary_file_buf[0], size);
    fin.close();

    HEADER udp_header(filename.length(), 0, START, stream_seq_order, 0);
    my_udp udp_packets(udp_header, filename.c_str());
    uint16_t check = checksum((uint16_t*)&udp_packets, UDP_LEN); // 计算校验和
    udp_packets.udp_header.cksum = check;

    int packet_num = size / DEFAULT_BUFLEN + 1;
    setConsoleColor(1);
    cout << " #####文件名校验和：" << check << endl;
    cout << " #####送数据包的数量：" << packet_num << endl;
    cout << " #####滑动窗口大小：" << N << endl;

    // 正常发送第一个文件名数据包
    send_packet(udp_packets, SendSocket, RecvAddr, packet_num);
    //创建计时器 
    clock_t start = clock();
    // 创建接收 ACK 的线程
    thread ack_thread(receive_ACK_thread, ref(SendSocket), ref(RecvAddr), ref(base), ref(ACK_index), ref(message_queue), ref(packet_num), ref(start));

    // 主线程发送数据包
    while (ACK_index < packet_num) {
        lock_guard<mutex> lock(mtx);  // 确保共享变量的安全访问

        // 检查是否可以发送新的数据包
        while (next_seqnum < base + N && next_seqnum <= packet_num) {
            uint16_t remainder = next_seqnum % DEFAULT_SEQNUM;
            if (next_seqnum == packet_num) {  // 最后一个包
                udp_header.set_value(size - (next_seqnum - 1) * DEFAULT_BUFLEN, 0, OVER, stream_seq_order, remainder);
                udp_packets.set_value(udp_header, binary_file_buf + (next_seqnum - 1) * DEFAULT_BUFLEN, size - (next_seqnum - 1) * DEFAULT_BUFLEN);
            }
            else {  // 正常包
                udp_header.set_value(DEFAULT_BUFLEN, 0, 0, stream_seq_order, remainder);
                udp_packets.set_value(udp_header, binary_file_buf + (next_seqnum - 1) * DEFAULT_BUFLEN, DEFAULT_BUFLEN);
            }

            udp_packets.udp_header.cksum = checksum((uint16_t*)&udp_packets, UDP_LEN);
            send_packet_GBN(udp_packets, SendSocket, RecvAddr);
            setConsoleColor(7);
            cout << "[SEND] Packet SEQ=" << remainder << " Checksum = " << udp_packets.udp_header.cksum << endl;
            message_queue.push(udp_packets);
            next_seqnum++;
        }

        // 检查超时并重发
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "[TIMEOUT] 重发未确认数据包." << endl;
            start = clock();

            queue<my_udp> temp_queue = message_queue;  // 创建副本用于重发
            while (!temp_queue.empty()) {
                send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                setConsoleColor(7);
                cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                temp_queue.pop();
            }
        }
    }

    // 等待接收线程结束
    ack_thread.join();
    setConsoleColor(10);
    cout << "#####文件传输完成！#####" << endl << endl;
    stream_seq_order++;
    check_stream_seq();
    delete[] binary_file_buf;
}


void send_file(string filename, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    // 每次发送一个文件的时候，先把seq_order序列号清零
    // 每次完成一个文件的发送后，stream_seq_order + 1
    seq_order = 0;
    ifstream fin(filename.c_str(), ifstream::binary);

    // 获取文件大小
    fin.seekg(0, std::ifstream::end);
    long size = fin.tellg();
    file_size = size;
    fin.seekg(0);

    char* binary_file_buf = new char[size];
    setConsoleColor(1);
    cout << "#####文件大小：" << size << " bytes" << endl;

    fin.read(&binary_file_buf[0], size);
    fin.close();

    // 第一个数据包要发送文件名，并且先只标记START
    HEADER udp_header(filename.length(), 0, START, stream_seq_order, seq_order);
    my_udp udp_packets(udp_header, filename.c_str());

    // 测试校验和 sizeof(HEADER): 8  sizeof(my_udp): 4104 = 4096 + 8
    uint16_t check = checksum((uint16_t*)&udp_packets, UDP_LEN); // 计算校验和
    udp_packets.udp_header.cksum = check;
    int packet_num = size / DEFAULT_BUFLEN + 1;
    setConsoleColor(1);
    cout << "#####发送数据包的数量：" << packet_num << endl << endl;

    send_packet(udp_packets, SendSocket, RecvAddr, packet_num);

    // 包含第一个文件名以及START标志，最后一个数据包带OVER标志
    for (int index = 0; index < packet_num; index++) {
        if (index == packet_num - 1) {
            udp_header.set_value(size - index * DEFAULT_BUFLEN, 0, OVER, stream_seq_order, seq_order); // seq序列号
            udp_packets.set_value(udp_header, binary_file_buf + index * DEFAULT_BUFLEN, size - index * DEFAULT_BUFLEN); // ??
            check = checksum((uint16_t*)&udp_packets, UDP_LEN);
            udp_packets.udp_header.cksum = check;
        }
        else {
            udp_header.set_value(DEFAULT_BUFLEN, 0, 0, stream_seq_order, seq_order);
            udp_packets.set_value(udp_header, binary_file_buf + index * DEFAULT_BUFLEN, DEFAULT_BUFLEN);
            check = checksum((uint16_t*)&udp_packets, UDP_LEN);
            udp_packets.udp_header.cksum = check;
        }

        // 增加测试部分，90%的概率按照正确的进行发送，10%的概率按照错误的进行发送
        int error_probability = rand() % 10;
        if (error_probability < 1) {
            udp_packets.udp_header.SEQ++;
            send_packet(udp_packets, SendSocket, RecvAddr, packet_num);
        }
        else {
            send_packet(udp_packets, SendSocket, RecvAddr, packet_num);
        }
        Sleep(10);

    }
    setConsoleColor(10);
    cout << "#####已成功发送文件！##### " << endl << endl;
    stream_seq_order++;
    check_stream_seq();
    delete[] binary_file_buf;
}

// 三次握手建立连接
bool Connect(SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    HEADER udp_header;
    udp_header.set_value(0, 0, SYN, 0, 0xFFFF); // 初始的SEQ给0xFFFF
    my_udp first_connect(udp_header); // 第一次握手

    uint16_t temp = checksum((uint16_t*)&first_connect, UDP_LEN);
    first_connect.udp_header.cksum = temp;


    int iResult = 0;
    int RecvAddrSize = sizeof(RecvAddr);
    char* connect_buffer = new char[UDP_LEN];

    memcpy(connect_buffer, &first_connect, UDP_LEN);
    iResult = sendto(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####第一次握手失败，请重启Sender#####" << endl;
        return 0;
    }

    clock_t start = clock(); //记录发送第一次握手发出时间
    u_long mode = 1;
    ioctlsocket(SendSocket, FIONBIO, &mode); // 设置成阻塞模式等待ACK响应

    // 接收第二次握手ACK响应，其中的ACK应当为0xFFFF+1 = 0
    while (recvfrom(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize) <= 0) {
        // rdt3.0: 超时，重新传输第一次握手
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "#####第一次握手超时，正在重传#####" << endl;
            iResult = sendto(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
            if (iResult == SOCKET_ERROR) {
                cout << "#####第一次握手重传Error，请重启Sender#####" << endl;
                return 0;
            }
            start = clock(); // 重设时间
        }
    }
    setConsoleColor(10);
    cout << "#####第一次握手成功！#####" << endl;

    // 获取到了第二次握手的ACK消息，检查校验和
    memcpy(&first_connect, connect_buffer, UDP_LEN);
    // 保存SYN_ACK的SEQ信息，完成k+1的验证
    uint16_t Recv_connect_Seq = 0x0;
    if (first_connect.udp_header.Flag == SYN_ACK && checksum((uint16_t*)&first_connect, UDP_LEN) == 0 && first_connect.udp_header.SEQ == 0xFFFF) {
        Recv_connect_Seq = 0xFFFF;
        setConsoleColor(10);
        cout << "#####第二次握手成功！#####" << endl;
    }
    else {
        setConsoleColor(12);
        cout << "#####第二次握手失败，请重启Sender#####" << endl;
        return 0;
    }

    // 第三次握手ACK，先清空缓冲区
    memset(&first_connect, 0, UDP_LEN);
    memset(connect_buffer, 0, UDP_LEN);
    first_connect.udp_header.Flag = ACK;
    first_connect.udp_header.SEQ = Recv_connect_Seq + 1; // 0
    first_connect.udp_header.cksum = checksum((uint16_t*)&first_connect, UDP_LEN);
    memcpy(connect_buffer, &first_connect, UDP_LEN);

    iResult = sendto(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####第三次握手失败，请重启Sender#####" << endl;
        return 0;
    }
    setConsoleColor(10);
    cout << "#####接收端已成功连接！现在可以发送文件！#####" << endl;

    delete[] connect_buffer;
    return 1;
}

bool disConnect(SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    int iResult = 0;
    int RecvAddrSize = sizeof(RecvAddr);
    char* disconnect_buffer = new char[UDP_LEN];

    HEADER udp_header;
    udp_header.set_value(0, 0, FIN, 0, 0xFFFF); // 同样的四次挥手，这里的设计是序列号互相均发送0xFFFF
    my_udp last_connect(udp_header); // 第一次FIN
    uint16_t temp = checksum((uint16_t*)&last_connect, UDP_LEN);
    last_connect.udp_header.cksum = temp;

    memcpy(disconnect_buffer, &last_connect, UDP_LEN);
    iResult = sendto(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####第一次挥手失败，请重启Sender#####" << endl;
        return 0;
    }

    clock_t start = clock();
    u_long mode = 1;
    ioctlsocket(SendSocket, FIONBIO, &mode); // 设置成阻塞模式等待ACK响应

    // 接收第二次挥手
    while (recvfrom(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize) <= 0) {
        // rdt3.0: 超时重传
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "#####第一次挥手超时，正在重传！#####" << endl;
            iResult = sendto(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
            if (iResult == SOCKET_ERROR) {
                setConsoleColor(12);
                cout << "#####第一次挥手失败，请重启Sender！#####" << endl;
                return 0;
            }
            start = clock(); // 重设时间
        }
    }
    setConsoleColor(10);
    cout << "#####完成第一次挥手 #####" << endl;
    cout << "#####接收到第二次挥手消息，进行验证 #####" << endl;

    memcpy(&last_connect, disconnect_buffer, UDP_LEN);
    if (last_connect.udp_header.Flag == ACK && checksum((uint16_t*)&last_connect, UDP_LEN) == 0 && last_connect.udp_header.SEQ == 0x0) {
        cout << "#####完成第二次挥手 #####" << endl;
    }
    else {
        setConsoleColor(12);
        cout << "#####第二次挥手失败，请重启Sender#####" << endl;
        return 0;
    }

    // 第三次挥手等待接收消息
    iResult = recvfrom(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####第三次挥手失败，请重启Sender#####" << endl;
        return 0;
    }
    else {
        setConsoleColor(10);
        cout << "#####接收到第三次挥手消息，进行验证 #####" << endl;
        memcpy(&last_connect, disconnect_buffer, UDP_LEN);
        if (last_connect.udp_header.Flag == FIN_ACK && checksum((uint16_t*)&last_connect, UDP_LEN) == 0 && last_connect.udp_header.SEQ == 0xFFFF) {
            setConsoleColor(10);
            cout << "#####完成第三次挥手 #####" << endl;
        }
        else {
            setConsoleColor(12);
            cout << "#####第三次挥手失败，请重启Sender#####" << endl;
            return 0;
        }
    }

    // 发送第四次挥手消息
    memset(&last_connect, 0, UDP_LEN);
    memset(disconnect_buffer, 0, UDP_LEN);
    last_connect.udp_header.Flag = ACK;
    last_connect.udp_header.SEQ = 0x0;
    last_connect.udp_header.cksum = checksum((uint16_t*)&last_connect, UDP_LEN);

    memcpy(disconnect_buffer, &last_connect, UDP_LEN);
    iResult = sendto(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####第四次挥手失败，请重启Sender#####" << endl;
        return 0;
    }
    setConsoleColor(10);
    cout << "#####完成第四次挥手 #####" << endl;
    return 1;
}

int main()
{

    int iResult;
    WSADATA wsaData;

    // 先初始化Socket的时候，初始化为Invalid
    SOCKET SendSocket = INVALID_SOCKET;
    sockaddr_in RecvAddr;

    //----------------------
    // 初始化WinSock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        setConsoleColor(12);
        cout << "WSAStartup failed with error: " << iResult << endl;
        return 1;
    }

    //---------------------------------------------
    // 创建发送socket来传输数据
    SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SendSocket == INVALID_SOCKET) {
        setConsoleColor(12);
        cout << "Socket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    //---------------------------------------------
    // 创建RecvAddr结构用本机IP和指定的端口号
    RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(DEFAULT_PORT);
    inet_pton(AF_INET, DEFAULT_IP, &RecvAddr.sin_addr.s_addr);


    //---------------------------------------------
    //建立连接
    if (Connect(SendSocket, RecvAddr)) {
        cout << "test success!" << endl;
    }
    else {
        cout << "test fail!" << endl;
        return 0;
    }

    //---------------------------------------------
    while (true) {
        string command;
        setConsoleColor(7);
        cout << "-------------你现在可以使用 quit 命令退出连接！------------" << endl;
        cout << "-----------------------------------------------------------" << endl << endl;
        cout << "#####请输入想要发送的文件名 #####" << endl;
        cin >> command;
        cout << endl;
        if (command == "quit") {
            HEADER command_header;
            command_header.set_value(4, 0, START_OVER, 0, 0);
            my_udp command_udp(command_header);
            char* command_buffer = new char[UDP_LEN];
            memcpy(command_buffer, &command_udp, UDP_LEN);

            if (sendto(SendSocket, command_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr)) == SOCKET_ERROR) {
                setConsoleColor(12);
                cout << "#####quit命令发送失败 #####" << endl;
                return 0;
            }
            else {
                cout << "#####quit命令发送成功 #####" << endl;
            }

            break;
        }
        else {
            clock_t start = clock();
            send_file_Reno(command, SendSocket, RecvAddr);
            clock_t end = clock();
            setConsoleColor(1);
            cout << "#####传输文件时间为：" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
            cout << "#####吞吐率为:" << ((float)file_size) / ((end - start) / CLOCKS_PER_SEC) << " bytes/s " << endl << endl;
            continue;
        }
    }

    //---------------------------------------------
    // 四次挥手断连，FIN
    if (disConnect(SendSocket, RecvAddr)) {
        cout << "test success!" << endl << endl;
    }
    else {
        setConsoleColor(12);
        cout << "test fail!" << endl << endl;
        return 0;
    }

    //---------------------------------------------
    // 当完成了相关的传输工作后，关闭socket
    iResult = closesocket(SendSocket);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "Closesocket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    //---------------------------------------------
    // 清理退出
    cout << "退出中..." << endl;
    WSACleanup();
    system("pause");
    return 0;
}