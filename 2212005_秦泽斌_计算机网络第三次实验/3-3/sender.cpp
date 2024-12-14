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
#define MAX_TIME  0.2 * CLOCKS_PER_SEC // ��ʱ�ش�

// ������ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

const uint16_t SYN = 0x1; // SYN = 1 ACK = 0 FIN = 0
const uint16_t ACK = 0x2; // SYN = 0, ACK = 1��FIN = 0
const uint16_t SYN_ACK = 0x3; // SYN = 1, ACK = 1
const uint16_t START = 0x10;
const uint16_t OVER = 0x8;
const uint16_t FIN = 0x4; // FIN = 1 ACK = 0
const uint16_t FIN_ACK = 0x6; // FIN = 1 ACK = 1
const uint16_t START_OVER = 0x18; // START = 1 OVER = 1

uint16_t seq_order = 0;
uint16_t stream_seq_order = 0;
long file_size = 0;
//Ϊ����������ɫ
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

    // ��ʼ��������STREAM��ǵ����ļ��Ŀ�ʼ�����
    // ��ʼ��ʱ����Ҫ���ļ��������ȥ�����Ը���λ��־λ�ֱ�ΪSTART OVER

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

// ����������ֺ��Ĵλ��ֵĳ�ʼ������
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

// ����У���
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

// ����ȫ�ֱ��������к���û�г������ֵ
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
    cout << "�ѷ������ݰ�" << udp2show.udp_header.SEQ << "!   ���ݰ���СΪ " << udp2show.udp_header.datasize << " bytes" << endl;
    cout << "У���:" << udp2show.udp_header.cksum << "     ACK:" << udp2show.udp_header.Flag << endl;
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

    // ��¼����ʱ�䣬��ʱ�ش�
    // �ȴ�����ACK��Ϣ����֤���к�
    clock_t start = clock();
    while (true) {
        while (recvfrom(SendSocket, RecvBuf, UDP_LEN, 0, (sockaddr*)&RecvAddr, &RecvAddrSize) <= 0) {

            if (clock() - start > MAX_TIME) {
                setConsoleColor(12);
                cout << "#####���䳬ʱ���������·������ݰ� " << Packet.udp_header.SEQ << "!#####" << endl;


                Packet.udp_header.SEQ = seq_order;
                memcpy(SendBuf, &Packet, UDP_LEN);
                iResult = sendto(SendSocket, SendBuf, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
                print_Send_information(Packet, "ReSend");
                start = clock(); // ���迪ʼʱ��
                if (iResult == SOCKET_ERROR) {
                    setConsoleColor(12);
                    cout << "Sendto failed with error: " << WSAGetLastError() << endl;

                }
            }
        }

        memcpy(&Recv_udp, RecvBuf, UDP_LEN);
        if (Recv_udp.udp_header.SEQ == Packet.udp_header.SEQ && Recv_udp.udp_header.Flag == ACK && checksum((uint16_t*)&Recv_udp, UDP_LEN) == 0) {
            setConsoleColor(7);
            cout << "���ݰ���ȷ�ϱ����ܣ�    " << " ��ǰ���ͽ���:" << Packet.udp_header.SEQ << "/" << packet_num << "   ACK:" << ACK << endl << endl;
            seq_order++; // ȫ�ֱ��������к�
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

// GBN���ֵ�ȫ�ֱ���
const int N = 32; // ���ڴ�С
uint16_t base = 1; // ��ʼΪ1
uint16_t next_seqnum = 1;
int ACK_index = 0; // �жϽ���
queue<my_udp> message_queue;
//Reno���ֵ�ȫ�ֱ���
int cwnd = 1;//ӵ������
int ssthresh = 16; // ��������ֵ
int duplicate_ACK_count = 0; // �ظ�ACK����
int RTT_ACK = 0;
// ���յ�ACKʱ��Ҫ���µĹ�������
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

//ӵ�����ƴ���
//���պ������߳�
void receive_ACK_thread_Reno(SOCKET& SendSocket, sockaddr_in& RecvAddr, uint16_t& base, int& ACK_index, queue<my_udp>& message_queue, int& packet_num, clock_t& start) {
    char RecvBuf[UDP_LEN];
    my_udp Recv_udp;
    int RecvAddrSize = sizeof(RecvAddr);
    int FastN = 0;
    int FastStatu = 0;


    while (ACK_index < packet_num) {  // �������� ACK��ֱ���������ݰ���ȷ��
        if (recvfrom(SendSocket, RecvBuf, UDP_LEN, 0, (sockaddr*)&RecvAddr, &RecvAddrSize) > 0) {
            memcpy(&Recv_udp, RecvBuf, UDP_LEN);

            // ����Ƿ�����Ч�� ACK ��
            if (Recv_udp.udp_header.Flag == ACK && checksum((uint16_t*)&Recv_udp, UDP_LEN) == 0) {
                lock_guard<mutex> lock(mtx);  // ���� base �Ͷ��еķ���
                if ((base % DEFAULT_SEQNUM) == Recv_udp.udp_header.SEQ) {
                    // ������������ȷ�ϵ����ݰ�
                    if (!message_queue.empty()) {
                        message_queue.pop();
                    }
                    start = clock();//���ü�ʱ��
                    base++;  // ��������ǰ��
                    ACK_index++;
                    setConsoleColor(7);
                    cout << "[ACK RECEIVED] base=" << base << ", ACK_index=" << ACK_index << endl;

                    //�յ���ȷ��ACK����Ϊ�������׶κ�ӵ������׶��������
                    FastStatu = 0;//�ָ������ش�Ϊ����״̬
                    FastN = 0;
                    duplicate_ACK_count = 0;//�����ظ�ACK����
                    if (cwnd < ssthresh) {
                        cwnd++;//�������׶Σ�ÿ�յ�һ����ȷACK������+1
                        RTT_ACK = 0;
                        setConsoleColor(1);
                        cout << "�������׶Σ�ÿ�յ�һ����ȷACK������+1, cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;
                    }
                    else {
                        RTT_ACK++;
                        if (RTT_ACK == cwnd) {
                            cwnd++;//ӵ������׶Σ�ÿ�յ�һ�����ڵ���ȷACK������+1
                            RTT_ACK = 0;
                            setConsoleColor(1);
                            cout << "ӵ������׶Σ�ÿ�յ�һ�����ڵ���ȷACK������+1, cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;
                        }
                    }

                }
                else {//�յ��˷�Ԥ�ڵ�ACK��
                    setConsoleColor(12);
                    cout << "[DUPLICATE ACK] SEQ=" << Recv_udp.udp_header.SEQ << endl;
                    duplicate_ACK_count++;
                    //�����յ������ظ�ACK�������������ش�
                    if (duplicate_ACK_count == 3&& FastStatu == 0) {
                        FastN++;
                        duplicate_ACK_count = 0;
                        RTT_ACK = 0;
                        ssthresh = cwnd / 2;
                        cwnd = ssthresh + 3;
                        setConsoleColor(1);
                        cout << "���ٻָ��׶Σ�cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;
                        start = clock();//���ü�ʱ��
                        queue<my_udp> temp_queue = message_queue;  // �������������ط�
                        while (!temp_queue.empty()) {
                            send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                            setConsoleColor(7);
                            cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                            temp_queue.pop();
                        }
                    }
                    if (FastN == 2) {
                        FastN = 0;
                        FastStatu = 1;//�������������ش�
                        setConsoleColor(12);
                        cout << "[TIMEOUT] �ط�δȷ�����ݰ�." << endl;
                        start = clock();
                        //��ʱ�ش������»ص�������״̬,��ֵ����
                        RTT_ACK = 0;
                        ssthresh = cwnd / 2;
                        cwnd = 1;
                        setConsoleColor(1);
                        cout << "���½����������׶Σ�cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;


                        queue<my_udp> temp_queue = message_queue;  // �������������ط�
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


// Reno�ķ����ļ�����
void send_file_Reno(string filename, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    Reno_init();
    int RecvAddrSize = sizeof(RecvAddr);

    // ��ȡ�ļ�����
    ifstream fin(filename.c_str(), ifstream::binary);
    fin.seekg(0, std::ifstream::end);
    long size = fin.tellg();
    file_size = size;
    fin.seekg(0);

    char* binary_file_buf = new char[size];
    setConsoleColor(1);
    cout << " #####�ļ���С��" << size << " bytes" << endl;
    fin.read(&binary_file_buf[0], size);
    fin.close();

    HEADER udp_header(filename.length(), 0, START, stream_seq_order, 0);
    my_udp udp_packets(udp_header, filename.c_str());
    uint16_t check = checksum((uint16_t*)&udp_packets, UDP_LEN); // ����У���
    udp_packets.udp_header.cksum = check;

    int packet_num = size / DEFAULT_BUFLEN + 1;
    setConsoleColor(1);
    cout << " #####�ļ���У��ͣ�" << check << endl;
    cout << " #####�����ݰ���������" << packet_num << endl;
    cout << " #####�������ڴ�С��" << cwnd << endl;

    // �������͵�һ���ļ������ݰ�
    send_packet(udp_packets, SendSocket, RecvAddr, packet_num);
    //������ʱ�� 
    clock_t start = clock();
    // �������� ACK ���߳�
    thread ack_thread(receive_ACK_thread_Reno, ref(SendSocket), ref(RecvAddr), ref(base), ref(ACK_index), ref(message_queue), ref(packet_num), ref(start));

    // ���̷߳������ݰ�
    while (ACK_index < packet_num) {
        lock_guard<mutex> lock(mtx);  // ȷ����������İ�ȫ����

        // ����Ƿ���Է����µ����ݰ�
        while (next_seqnum < base + cwnd && next_seqnum <= packet_num) {
            uint16_t remainder = next_seqnum % DEFAULT_SEQNUM;
            if (next_seqnum == packet_num) {  // ���һ����
                udp_header.set_value(size - (next_seqnum - 1) * DEFAULT_BUFLEN, 0, OVER, stream_seq_order, remainder);
                udp_packets.set_value(udp_header, binary_file_buf + (next_seqnum - 1) * DEFAULT_BUFLEN, size - (next_seqnum - 1) * DEFAULT_BUFLEN);
            }
            else {  // ������
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

        // ��鳬ʱ���ط�
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "[TIMEOUT] �ط�δȷ�����ݰ�." << endl;
            start = clock();
            //��ʱ�ش������»ص�������״̬,��ֵ����
            RTT_ACK = 0;
            ssthresh = cwnd / 2;
            cwnd = 1;
            setConsoleColor(1);
            cout << "���½����������׶Σ�cwnd = " << cwnd << "    ssthresh = " << ssthresh << endl;


            queue<my_udp> temp_queue = message_queue;  // �������������ط�
            while (!temp_queue.empty()) {
                send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                setConsoleColor(7);
                cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                temp_queue.pop();
            }
        }
    }

    // �ȴ������߳̽���
    ack_thread.join();
    setConsoleColor(10);
    cout << "#####�ļ�������ɣ�#####" << endl << endl;
    stream_seq_order++;
    check_stream_seq();
    delete[] binary_file_buf;
}

//���պ������߳�
void receive_ACK_thread(SOCKET& SendSocket, sockaddr_in& RecvAddr, uint16_t& base, int& ACK_index, queue<my_udp>& message_queue, int& packet_num, clock_t& start) {
    char RecvBuf[UDP_LEN];
    my_udp Recv_udp;
    int RecvAddrSize = sizeof(RecvAddr);

    while (ACK_index < packet_num) {  // �������� ACK��ֱ���������ݰ���ȷ��
        if (recvfrom(SendSocket, RecvBuf, UDP_LEN, 0, (sockaddr*)&RecvAddr, &RecvAddrSize) > 0) {
            memcpy(&Recv_udp, RecvBuf, UDP_LEN);

            // ����Ƿ�����Ч�� ACK ��
            if (Recv_udp.udp_header.Flag == ACK && checksum((uint16_t*)&Recv_udp, UDP_LEN) == 0) {
                lock_guard<mutex> lock(mtx);  // ���� base �Ͷ��еķ���
                if ((base % DEFAULT_SEQNUM) == Recv_udp.udp_header.SEQ) {
                    // ������������ȷ�ϵ����ݰ�
                    if (!message_queue.empty()) {
                        message_queue.pop();
                    }
                    start = clock();//���ü�ʱ��
                    base++;  // ��������ǰ��
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


// GBN�ķ����ļ�����
void send_file_GBN(string filename, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    GBN_init();
    int RecvAddrSize = sizeof(RecvAddr);

    // ��ȡ�ļ�����
    ifstream fin(filename.c_str(), ifstream::binary);
    fin.seekg(0, std::ifstream::end);
    long size = fin.tellg();
    file_size = size;
    fin.seekg(0);

    char* binary_file_buf = new char[size];
    setConsoleColor(1);
    cout << " #####�ļ���С��" << size << " bytes" << endl;
    fin.read(&binary_file_buf[0], size);
    fin.close();

    HEADER udp_header(filename.length(), 0, START, stream_seq_order, 0);
    my_udp udp_packets(udp_header, filename.c_str());
    uint16_t check = checksum((uint16_t*)&udp_packets, UDP_LEN); // ����У���
    udp_packets.udp_header.cksum = check;

    int packet_num = size / DEFAULT_BUFLEN + 1;
    setConsoleColor(1);
    cout << " #####�ļ���У��ͣ�" << check << endl;
    cout << " #####�����ݰ���������" << packet_num << endl;
    cout << " #####�������ڴ�С��" << N << endl;

    // �������͵�һ���ļ������ݰ�
    send_packet(udp_packets, SendSocket, RecvAddr, packet_num);
    //������ʱ�� 
    clock_t start = clock();
    // �������� ACK ���߳�
    thread ack_thread(receive_ACK_thread, ref(SendSocket), ref(RecvAddr), ref(base), ref(ACK_index), ref(message_queue), ref(packet_num), ref(start));

    // ���̷߳������ݰ�
    while (ACK_index < packet_num) {
        lock_guard<mutex> lock(mtx);  // ȷ����������İ�ȫ����

        // ����Ƿ���Է����µ����ݰ�
        while (next_seqnum < base + N && next_seqnum <= packet_num) {
            uint16_t remainder = next_seqnum % DEFAULT_SEQNUM;
            if (next_seqnum == packet_num) {  // ���һ����
                udp_header.set_value(size - (next_seqnum - 1) * DEFAULT_BUFLEN, 0, OVER, stream_seq_order, remainder);
                udp_packets.set_value(udp_header, binary_file_buf + (next_seqnum - 1) * DEFAULT_BUFLEN, size - (next_seqnum - 1) * DEFAULT_BUFLEN);
            }
            else {  // ������
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

        // ��鳬ʱ���ط�
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "[TIMEOUT] �ط�δȷ�����ݰ�." << endl;
            start = clock();

            queue<my_udp> temp_queue = message_queue;  // �������������ط�
            while (!temp_queue.empty()) {
                send_packet_GBN(temp_queue.front(), SendSocket, RecvAddr);
                setConsoleColor(7);
                cout << "[RESEND] Packet SEQ=" << temp_queue.front().udp_header.SEQ << " resent." << endl;
                temp_queue.pop();
            }
        }
    }

    // �ȴ������߳̽���
    ack_thread.join();
    setConsoleColor(10);
    cout << "#####�ļ�������ɣ�#####" << endl << endl;
    stream_seq_order++;
    check_stream_seq();
    delete[] binary_file_buf;
}


void send_file(string filename, SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    // ÿ�η���һ���ļ���ʱ���Ȱ�seq_order���к�����
    // ÿ�����һ���ļ��ķ��ͺ�stream_seq_order + 1
    seq_order = 0;
    ifstream fin(filename.c_str(), ifstream::binary);

    // ��ȡ�ļ���С
    fin.seekg(0, std::ifstream::end);
    long size = fin.tellg();
    file_size = size;
    fin.seekg(0);

    char* binary_file_buf = new char[size];
    setConsoleColor(1);
    cout << "#####�ļ���С��" << size << " bytes" << endl;

    fin.read(&binary_file_buf[0], size);
    fin.close();

    // ��һ�����ݰ�Ҫ�����ļ�����������ֻ���START
    HEADER udp_header(filename.length(), 0, START, stream_seq_order, seq_order);
    my_udp udp_packets(udp_header, filename.c_str());

    // ����У��� sizeof(HEADER): 8  sizeof(my_udp): 4104 = 4096 + 8
    uint16_t check = checksum((uint16_t*)&udp_packets, UDP_LEN); // ����У���
    udp_packets.udp_header.cksum = check;
    int packet_num = size / DEFAULT_BUFLEN + 1;
    setConsoleColor(1);
    cout << "#####�������ݰ���������" << packet_num << endl << endl;

    send_packet(udp_packets, SendSocket, RecvAddr, packet_num);

    // ������һ���ļ����Լ�START��־�����һ�����ݰ���OVER��־
    for (int index = 0; index < packet_num; index++) {
        if (index == packet_num - 1) {
            udp_header.set_value(size - index * DEFAULT_BUFLEN, 0, OVER, stream_seq_order, seq_order); // seq���к�
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

        // ���Ӳ��Բ��֣�90%�ĸ��ʰ�����ȷ�Ľ��з��ͣ�10%�ĸ��ʰ��մ���Ľ��з���
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
    cout << "#####�ѳɹ������ļ���##### " << endl << endl;
    stream_seq_order++;
    check_stream_seq();
    delete[] binary_file_buf;
}

// �������ֽ�������
bool Connect(SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    HEADER udp_header;
    udp_header.set_value(0, 0, SYN, 0, 0xFFFF); // ��ʼ��SEQ��0xFFFF
    my_udp first_connect(udp_header); // ��һ������

    uint16_t temp = checksum((uint16_t*)&first_connect, UDP_LEN);
    first_connect.udp_header.cksum = temp;


    int iResult = 0;
    int RecvAddrSize = sizeof(RecvAddr);
    char* connect_buffer = new char[UDP_LEN];

    memcpy(connect_buffer, &first_connect, UDP_LEN);
    iResult = sendto(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####��һ������ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }

    clock_t start = clock(); //��¼���͵�һ�����ַ���ʱ��
    u_long mode = 1;
    ioctlsocket(SendSocket, FIONBIO, &mode); // ���ó�����ģʽ�ȴ�ACK��Ӧ

    // ���յڶ�������ACK��Ӧ�����е�ACKӦ��Ϊ0xFFFF+1 = 0
    while (recvfrom(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize) <= 0) {
        // rdt3.0: ��ʱ�����´����һ������
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "#####��һ�����ֳ�ʱ�������ش�#####" << endl;
            iResult = sendto(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
            if (iResult == SOCKET_ERROR) {
                cout << "#####��һ�������ش�Error��������Sender#####" << endl;
                return 0;
            }
            start = clock(); // ����ʱ��
        }
    }
    setConsoleColor(10);
    cout << "#####��һ�����ֳɹ���#####" << endl;

    // ��ȡ���˵ڶ������ֵ�ACK��Ϣ�����У���
    memcpy(&first_connect, connect_buffer, UDP_LEN);
    // ����SYN_ACK��SEQ��Ϣ�����k+1����֤
    uint16_t Recv_connect_Seq = 0x0;
    if (first_connect.udp_header.Flag == SYN_ACK && checksum((uint16_t*)&first_connect, UDP_LEN) == 0 && first_connect.udp_header.SEQ == 0xFFFF) {
        Recv_connect_Seq = 0xFFFF;
        setConsoleColor(10);
        cout << "#####�ڶ������ֳɹ���#####" << endl;
    }
    else {
        setConsoleColor(12);
        cout << "#####�ڶ�������ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }

    // ����������ACK������ջ�����
    memset(&first_connect, 0, UDP_LEN);
    memset(connect_buffer, 0, UDP_LEN);
    first_connect.udp_header.Flag = ACK;
    first_connect.udp_header.SEQ = Recv_connect_Seq + 1; // 0
    first_connect.udp_header.cksum = checksum((uint16_t*)&first_connect, UDP_LEN);
    memcpy(connect_buffer, &first_connect, UDP_LEN);

    iResult = sendto(SendSocket, connect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####����������ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }
    setConsoleColor(10);
    cout << "#####���ն��ѳɹ����ӣ����ڿ��Է����ļ���#####" << endl;

    delete[] connect_buffer;
    return 1;
}

bool disConnect(SOCKET& SendSocket, sockaddr_in& RecvAddr) {
    int iResult = 0;
    int RecvAddrSize = sizeof(RecvAddr);
    char* disconnect_buffer = new char[UDP_LEN];

    HEADER udp_header;
    udp_header.set_value(0, 0, FIN, 0, 0xFFFF); // ͬ�����Ĵλ��֣��������������кŻ��������0xFFFF
    my_udp last_connect(udp_header); // ��һ��FIN
    uint16_t temp = checksum((uint16_t*)&last_connect, UDP_LEN);
    last_connect.udp_header.cksum = temp;

    memcpy(disconnect_buffer, &last_connect, UDP_LEN);
    iResult = sendto(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####��һ�λ���ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }

    clock_t start = clock();
    u_long mode = 1;
    ioctlsocket(SendSocket, FIONBIO, &mode); // ���ó�����ģʽ�ȴ�ACK��Ӧ

    // ���յڶ��λ���
    while (recvfrom(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize) <= 0) {
        // rdt3.0: ��ʱ�ش�
        if (clock() - start > MAX_TIME) {
            setConsoleColor(12);
            cout << "#####��һ�λ��ֳ�ʱ�������ش���#####" << endl;
            iResult = sendto(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
            if (iResult == SOCKET_ERROR) {
                setConsoleColor(12);
                cout << "#####��һ�λ���ʧ�ܣ�������Sender��#####" << endl;
                return 0;
            }
            start = clock(); // ����ʱ��
        }
    }
    setConsoleColor(10);
    cout << "#####��ɵ�һ�λ��� #####" << endl;
    cout << "#####���յ��ڶ��λ�����Ϣ��������֤ #####" << endl;

    memcpy(&last_connect, disconnect_buffer, UDP_LEN);
    if (last_connect.udp_header.Flag == ACK && checksum((uint16_t*)&last_connect, UDP_LEN) == 0 && last_connect.udp_header.SEQ == 0x0) {
        cout << "#####��ɵڶ��λ��� #####" << endl;
    }
    else {
        setConsoleColor(12);
        cout << "#####�ڶ��λ���ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }

    // �����λ��ֵȴ�������Ϣ
    iResult = recvfrom(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####�����λ���ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }
    else {
        setConsoleColor(10);
        cout << "#####���յ������λ�����Ϣ��������֤ #####" << endl;
        memcpy(&last_connect, disconnect_buffer, UDP_LEN);
        if (last_connect.udp_header.Flag == FIN_ACK && checksum((uint16_t*)&last_connect, UDP_LEN) == 0 && last_connect.udp_header.SEQ == 0xFFFF) {
            setConsoleColor(10);
            cout << "#####��ɵ����λ��� #####" << endl;
        }
        else {
            setConsoleColor(12);
            cout << "#####�����λ���ʧ�ܣ�������Sender#####" << endl;
            return 0;
        }
    }

    // ���͵��Ĵλ�����Ϣ
    memset(&last_connect, 0, UDP_LEN);
    memset(disconnect_buffer, 0, UDP_LEN);
    last_connect.udp_header.Flag = ACK;
    last_connect.udp_header.SEQ = 0x0;
    last_connect.udp_header.cksum = checksum((uint16_t*)&last_connect, UDP_LEN);

    memcpy(disconnect_buffer, &last_connect, UDP_LEN);
    iResult = sendto(SendSocket, disconnect_buffer, UDP_LEN, 0, (SOCKADDR*)&RecvAddr, RecvAddrSize);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "#####���Ĵλ���ʧ�ܣ�������Sender#####" << endl;
        return 0;
    }
    setConsoleColor(10);
    cout << "#####��ɵ��Ĵλ��� #####" << endl;
    return 1;
}

int main()
{

    int iResult;
    WSADATA wsaData;

    // �ȳ�ʼ��Socket��ʱ�򣬳�ʼ��ΪInvalid
    SOCKET SendSocket = INVALID_SOCKET;
    sockaddr_in RecvAddr;

    //----------------------
    // ��ʼ��WinSock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        setConsoleColor(12);
        cout << "WSAStartup failed with error: " << iResult << endl;
        return 1;
    }

    //---------------------------------------------
    // ��������socket����������
    SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SendSocket == INVALID_SOCKET) {
        setConsoleColor(12);
        cout << "Socket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    //---------------------------------------------
    // ����RecvAddr�ṹ�ñ���IP��ָ���Ķ˿ں�
    RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(DEFAULT_PORT);
    inet_pton(AF_INET, DEFAULT_IP, &RecvAddr.sin_addr.s_addr);


    //---------------------------------------------
    //��������
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
        cout << "-------------�����ڿ���ʹ�� quit �����˳����ӣ�------------" << endl;
        cout << "-----------------------------------------------------------" << endl << endl;
        cout << "#####��������Ҫ���͵��ļ��� #####" << endl;
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
                cout << "#####quit�����ʧ�� #####" << endl;
                return 0;
            }
            else {
                cout << "#####quit����ͳɹ� #####" << endl;
            }

            break;
        }
        else {
            clock_t start = clock();
            send_file_Reno(command, SendSocket, RecvAddr);
            clock_t end = clock();
            setConsoleColor(1);
            cout << "#####�����ļ�ʱ��Ϊ��" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
            cout << "#####������Ϊ:" << ((float)file_size) / ((end - start) / CLOCKS_PER_SEC) << " bytes/s " << endl << endl;
            continue;
        }
    }

    //---------------------------------------------
    // �Ĵλ��ֶ�����FIN
    if (disConnect(SendSocket, RecvAddr)) {
        cout << "test success!" << endl << endl;
    }
    else {
        setConsoleColor(12);
        cout << "test fail!" << endl << endl;
        return 0;
    }

    //---------------------------------------------
    // ���������صĴ��乤���󣬹ر�socket
    iResult = closesocket(SendSocket);
    if (iResult == SOCKET_ERROR) {
        setConsoleColor(12);
        cout << "Closesocket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    //---------------------------------------------
    // �����˳�
    cout << "�˳���..." << endl;
    WSACleanup();
    system("pause");
    return 0;
}