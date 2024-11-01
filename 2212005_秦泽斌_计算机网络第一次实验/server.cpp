#include"server.h"
using namespace std;

// 部分可变化的全局量，支持自定义
u_short ServerPort = 12260;// 服务器端口号
char ServerIP[16];// 服务器IP地址
const int SBUF_SIZE = 2048;// 定义缓冲区大小
const int ListenMax = 100;// 监听最大队列数
int ConnectedNumber = 0;// 当前连接的用户数量

// 全局变量声明
SOCKET ServerSocket;// sever服务器用来接收，client用来存放收到的请求
SOCKET ClientSockets[ListenMax];
SOCKADDR_IN ServerAddr;// 用来存放IP地址和端口
SOCKADDR_IN ClientAddrs[ListenMax];
HANDLE ClientThreads[ListenMax];

int snaddr = sizeof(SOCKADDR_IN);
char SsendBuf[SBUF_SIZE];// 发送缓冲区
char SinputBuf[SBUF_SIZE];// 输入缓冲区
char SrecvBuf[SBUF_SIZE];//接收内容缓冲区
char hello[] = u8"Connected! Welcome to our char room.\0";
//wchar_t hello[] = L"连接成功！欢迎来到聊天室。\0";

char errorMessage[] = "Input destination user error, please check your input.\nPlease use 'getUsrList' to get users.\0";

struct para {//自定义结构体，用来存放用户信息
	int number;
	int len;
	char name[20];
	bool used = false;
};
para ClientInformation[ListenMax];

int getPort() {//获取端口号
	cout << "***请输入目标连接端口号,若使用默认端口号，请输入0\n";
	u_short tempPort;
	cin >> tempPort;
	while (tempPort < 12000 || tempPort>16000) {
		if (tempPort == 0) {
			return ServerPort;
		}
		cout << "非法输入！\n";
		cin >> tempPort;
	}
	return tempPort;
}

void getServerIP(char* p) {//获取本地服务器IP
	cout << "***请输入目标连接的IP地址以创建聊天室\n";
	char ip[16];
	cin.getline(ip, sizeof(ip));
	int index = 0;
	while (ip[index] != '\0') {
		p[index] = ip[index];
		index++;
	}
}

bool isName(char* a) {//判断是否是名字。根据字符串的第一个字符判断（自定义语义信息）
	if (a[0] == 'n')
		return true;
	else
		return false;
}

bool sisClientQuit(char* a) {//这个函数有误，没用
	int nameLen1 = int(a[25]);
	int nameLen2 = int(a[25 + nameLen1 + 1]);
	int len = 0;
	while (a[len] != '\0')
		len++;
	if ((len == (25 + 1 + nameLen1 + 1 + nameLen2 + 4)) && a[25 + 1 + nameLen1 + 1 + nameLen2] == 'q' && a[25 + 1 + nameLen1 + 1 + nameLen2 + 1] == 'u' && a[25 + 1 + nameLen1 + 1 + nameLen2 + 2] == 'i' && a[25 + 1 + nameLen1 + 1 + nameLen2 + 3] == 't')
		return true;
	else
		return false;
}

bool isSameStr(char* a, char* b) {//判断是否是相同用户名
	int i = 0;
	bool flag = true;
	while (a[i] != '\0' && b[i] != '\0') {
		if (a[i] != b[i]) {
			flag = false;
			break;
		}
		i++;
	}
	if (a[i] != '\0' || b[i] != '\0')
		flag = false;
	return flag;
}

bool isGetList(char* a) {//用户请求getUsrList
	const char* sample = "users\0";
	if (!strcmp(sample, a))
		return true;
	else
		return false;
}

bool isOpen(char* a) {//用户请求公屏发言
	const char* sample = "open\0";
	if (!strcmp(sample, a))
		return true;
	else
		return false;
}

DWORD WINAPI recvMessage(LPVOID tempPara) {
	para* p = (para*)tempPara;
	int num = p->number;
	while (1) {
		if (recv(ClientSockets[num], SrecvBuf, SBUF_SIZE, 0) < 0) {
		}
		else {
			if (isGetList(SrecvBuf)) {//用户输入的是getUsrList
				char usrList[SBUF_SIZE];
				memset(usrList, 0, SBUF_SIZE);
				usrList[0] = 'u';//第一个字母定义为u
				int posi = 1;
				for (int tempIndex = 0; tempIndex < ListenMax; tempIndex++) {//封装usrList并发送
					if (ClientInformation[tempIndex].used) {
						usrList[posi] = char(ClientInformation[tempIndex].len);
						posi += 1;
						strcat(usrList, ClientInformation[tempIndex].name);
						posi += ClientInformation[tempIndex].len;
					}
				}
				send(ClientSockets[num], usrList, sizeof(usrList), 0);
			}
			else {
				if (SrecvBuf[0] == 'q') {//有某个用户下线quit
					int len = int(SrecvBuf[1]);
					char* nameString = new char[len + 1];
					for (int index = 0; index < len; index++)
						nameString[index] = SrecvBuf[index + 2];//因为第一个字符为q，第二个字符为用户名长度信息，用户名从第三个字符开始
					nameString[len] = '\0';
					cout << "***用户: " << nameString << " 下线了.\n";
					ConnectedNumber -= 1;
					cout << "***当前用户数量为 " << ConnectedNumber << "\n";
					// 给当前在线的每个客户都发一遍消息（除了自己）
					for (int i = 0; i < ListenMax; i++) {
						if (i == num || ClientSockets[i] == INVALID_SOCKET)
							continue;
						else
							send(ClientSockets[i], SrecvBuf, sizeof(SrecvBuf), 0);
					}
					closesocket(ClientSockets[num]);
					ClientInformation[num].used = false;
					break;
				}
				else {
					// 重新封装消息
					char timeString[26];//时间信息
					int index = 0;
					for (; index < 25; index++)
						timeString[index] = SrecvBuf[index];
					timeString[25] = '\0';
					int len = int(SrecvBuf[index]);//目标用户长度
					index += 1;
					char* dstName = new char[len + 1];//目标用户名称
					for (; index < 26 + len; index++)
						dstName[index - 26] = SrecvBuf[index];
					dstName[len] = '\0';
					len = int(SrecvBuf[index]);//来源用户长度
					index += 1;
					int lastPosi = len + index;
					int nowPost = index - 1;
					char* srcNameString = new char[len + 2];
					srcNameString[0] = char(len);//来源用户名称
					for (; index < lastPosi; index++)
						srcNameString[index - nowPost] = SrecvBuf[index];
					srcNameString[len + 1] = '\0';

					char tempBuf[SBUF_SIZE];//真正的消息内容
					len = 0;
					char mess[SBUF_SIZE];
					while (SrecvBuf[index] != '\0') {
						mess[len] = SrecvBuf[index];
						len++;
						index++;
					}
					mess[len] = '\0';
					memset(tempBuf, 0, SBUF_SIZE);
					strcat(tempBuf, timeString);
					strcat(tempBuf, srcNameString);
					strcat(tempBuf, mess);

					if (isOpen(dstName)) {//公共发言
						cout << "----------------------------------------------" << timeString << "来自" << srcNameString + 1 << "的消息：";
						int idx = 0;
						while (mess[idx] != '\0') {
							cout << mess[idx];
							idx++;
						}
						cout << endl;
						// 广播消息给所有在线用户
						for (int i = 0; i < ListenMax; i++) {
							if (ClientInformation[i].used && ClientSockets[i] != INVALID_SOCKET) {
								send(ClientSockets[i], tempBuf, sizeof(tempBuf), 0);
							}
						}
					}
					else {
						// 非公屏发言，则要发送给对应的用户
						int dst = 0;
						for (; dst < ListenMax; dst++) {
							if (ClientInformation[dst].used) {
								if (isSameStr(ClientInformation[dst].name, dstName)) {
									// 发送消息
									send(ClientSockets[ClientInformation[dst].number], tempBuf, sizeof(tempBuf), 0);
									break;
								}
							}
						}
						if (dst == ListenMax)//用户输入有误情况处理
						{
							char* temp = new char(strlen(srcNameString));
							temp = srcNameString + 1;
							for (int j = 0; j < ListenMax; j++)
							{
								if (ClientInformation[j].used) {
									if (strcmp(ClientInformation[j].name, temp) == 0)
									{
										send(ClientSockets[ClientInformation[j].number], errorMessage, sizeof(tempBuf), 0);
										break;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}

int serverMain() {

	// 加载socket库
	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata)) {
		//如果加载出错，暂停服务，输出出错信息
		cout << "载入socket失败\n";
		system("pause");
		return -1;
	}

	// 创建socket，参数顺序:IPV4，流式套接字，指定协议
	ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (ServerSocket == INVALID_SOCKET)
	{
		cout << "socket error:" << WSAGetLastError() << endl;
		return 0;
	}

	// 初始化地址信息
	// inet_addr用来将IP地址转换成网络字节序
	ServerAddr.sin_family = AF_INET;
	getServerIP(ServerIP);
	inet_pton(AF_INET, ServerIP, &ServerAddr.sin_addr.S_un.S_addr);
	ServerPort = getPort();
	//cin.get();
	ServerAddr.sin_port = htons(ServerPort);

	// 绑定socket和地址
	bind(ServerSocket, (SOCKADDR*)&ServerAddr, sizeof(SOCKADDR));

	cout << "***服务器已成功启动，本聊天室的IP地址是：" << ServerIP << "\t端口号为： " << ServerPort << endl;

	// 开始循环监听
	while (1) {
		if (ConnectedNumber == 0)
			cout << "***等待客户端接入中......\n";
		// 设置队列长度为5
		listen(ServerSocket, ListenMax);

		// accept新来的请求，并用一个socket存放新创建的socket
		int i = 0;
		for (; i < ListenMax; i++)
			if (ClientSockets[i] == 0)
				break;
		ClientSockets[i] = accept(ServerSocket, (SOCKADDR*)&ClientAddrs[i], &snaddr);
		// 检查新创建的socket是否有效
		if (ClientSockets[i] != INVALID_SOCKET) {
			ClientInformation[i].number = i;
			// 连接成功
			ConnectedNumber += 1;
			cout << "***客户端连接成功，当前用户数量为：" << ConnectedNumber << endl;

			memset(SrecvBuf, 0, SBUF_SIZE);
			recv(ClientSockets[i], SrecvBuf, SBUF_SIZE, 0);
			if (isName(SrecvBuf)) {//判断是否是名字；如果是，则放进name表中,并且填写ClientInformation相关信息
				ClientInformation[i].len = int(SrecvBuf[1]);
				int indexOfName = 2;
				while (SrecvBuf[indexOfName] != '\0') {
					ClientInformation[i].name[indexOfName - 2] = SrecvBuf[indexOfName];//SrecvBuf[0]为语义动作，[1]为len，[2]开始才是name
					indexOfName++;
				}
				ClientInformation[i].name[int(SrecvBuf[1]) + 1] = '\0';
				ClientInformation[i].used = true;
				for (int j = 0; j < 5; j++)
					if (ClientInformation[j].used == true)
						cout << "***当前在线用户: " << ClientInformation[j].name << endl;
			}
			// 告诉客户端连接成功
			// 获取当前时间
			time_t now = time(0);
			// 转换成字符串形式
			char* dt = ctime(&now);
			memset(SsendBuf, 0, SBUF_SIZE);
			strcat(SsendBuf, dt);
			strcat(SsendBuf, hello);
			//WideCharToMultiByte(CP_UTF8, 0, hello, -1, SsendBuf, SBUF_SIZE, NULL, NULL);
			//send(ClientSockets[i], SsendBuf, SBUF_SIZE, 0);//发送连接成功的消息
			//delete[] dt;
			memset(SsendBuf, 0, SBUF_SIZE);
			ClientThreads[i] = CreateThread(NULL, NULL, recvMessage, &ClientInformation[i], 0, NULL);//为每个连接成功client创建接收消息的线程

			if (ClientThreads[i] == 0) {
				cout << "线程创建失败，程序终止！\n";
				ConnectedNumber -= 1;
				return -1;
			}
		}
		else {
			cout << "连接失败！\n";
		}
	}

	closesocket(ServerSocket);

	WSACleanup();
	cout << "***服务器已关闭\n";
	return 0;
}

int main() {
	serverMain();
}