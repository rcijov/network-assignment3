#include "server.h"
using namespace std;

//////////////////////////////////////////////////////////
//
//	Programmed by
//		Razvan Alin Cijov
//
//////////////////////////////////////////////////////////

bool fileExists(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	return (result == 0);
}

long fileSize(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	if (result != 0) return 0;
	return stat_buf.st_size;
}

int Send(int sock, Handshake * handshake, Frame * frame, Acknowledgment * ack)
{
	int bytes = (handshake != nullptr) ? sendto(sock, (const char *)handshake, sizeof(*handshake), 0, (struct sockaddr*)&sa_in, sizeof(sa_in)) :
		(frame != nullptr ? sendto(sock, (const char*)frame, sizeof(*frame), 0, (struct sockaddr*)&sa_in, sizeof(sa_in)) :
		sendto(sock, (const char*)ack, sizeof(*ack), 0, (struct sockaddr*)&sa_in, sizeof(sa_in)));
	return bytes;
}

void err_sys(char * fmt, ...)
{
	perror(NULL);
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	printf(("Press Enter to exit.\n")); getchar();
	exit(1);
}

Package Receive(int sock, Frame * frame, Handshake * handshake, Acknowledgment * ack)
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	int bytes_recvd;
	int outfds = select(1, &readfds, NULL, NULL, &timeouts);
	switch (outfds)
	{
	case 0:
		return TIMEOUT; break;
	case 1:
		if (frame != nullptr)
		{
			bytes_recvd = recvfrom(sock, (char *)frame, sizeof(*frame), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		}
		else if (handshake != nullptr)
		{
			bytes_recvd = recvfrom(sock, (char *)handshake, sizeof(*handshake), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		}
		else if (ack != nullptr)
		{
			bytes_recvd = recvfrom(sock, (char *)ack, sizeof(*ack), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		}
		return INCOMING; break;
	default:
		return RECEIVE_ERROR; break;
	}
}

void createList()
{
	DIR *mydir = opendir(".");

	struct dirent *entry = NULL;
	char files[1028] = "";

	while ((entry = readdir(mydir)))
	{
		strcat(entry->d_name, "\r\n");
		strcat(files, entry->d_name);
	}

	closedir(mydir);

	FILE *file = fopen("List/list.txt", "w");

	int results = fputs(files, file);
	if (results == EOF) { fout << "Error writing the list file"; }
	fclose(file);
}

bool getFile(int sock, char * filename, char * sending_hostname, int client_number, bool list)
{
	Frame frame; frame.packetType = FRAME;
	Acknowledgment ack; ack.number = -1;
	long bytesCounter = 0, byteCount = 0;
	int bytesSent = 0, bytesRead = 0, bytesReadTotal = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	bool firstPacket = true, finished = false;
	int sequenceNumber = client_number % 2;
	int tries; bool maxAttempts = false;

	fout << "Sender started on host " << sending_hostname << endl;

	if (list)
	{
		createList();
	}

	FILE * stream = fopen(filename, "r+b");

	if (stream != NULL)
	{
		bytesCounter = fileSize(filename);
		while (1)
		{
			if (bytesCounter > MAX_FRAME_SIZE)
			{
				frame.header = (firstPacket ? INITIAL_DATA : DATA);
				byteCount = MAX_FRAME_SIZE;
			}
			else
			{
				byteCount = bytesCounter;
				finished = true;
				frame.header = FINAL_DATA;
			}

			bytesCounter -= MAX_FRAME_SIZE;
			bytesRead = fread(frame.buffer, sizeof(char), byteCount, stream);
			bytesReadTotal += bytesRead;
			frame.buffer_length = byteCount;
			frame.seqWidth = sequenceNumber;

			tries = 0;
			do {
				tries++;
				if (Send(sock, nullptr, &frame, nullptr) != sizeof(frame)) { return false; }
				packetsSent++;
				if (tries == 1) { packetsSentNeeded++; }
				bytesSent += sizeof(frame);

				fout << "Sender: sent frame " << sequenceNumber << endl;

				if (finished && (tries > MAX_TRIES))
				{
					maxAttempts = true;
					break;
				}

			} while (Receive(sock, nullptr, nullptr, &ack) != INCOMING || ack.number != sequenceNumber);

			if (maxAttempts)
			{
				fout << "Sender: did not receive ACK " << sequenceNumber << " after " << MAX_TRIES << " tries. Transfer finished." << endl;
			}
			else
			{
				fout << "Sender: received ACK " << ack.number << endl;
			}

			firstPacket = false;

			sequenceNumber = (sequenceNumber == 0 ? 1 : 0);

			if (finished) { break; }
		}

		fclose(stream);
		cout << "Sender: file transfer complete" << endl;
		cout << "Sender: number of packets sent: " << packetsSent << endl;
		fout << "Sender: file transfer complete" << endl;
		fout << "Sender: number of packets sent: " << packetsSent << endl;
		fout << "Sender: number of packets sent (needed): " << packetsSentNeeded << endl;
		fout << "Sender: number of bytes sent: " << bytesSent << endl;
		fout << "Sender: number of bytes read: " << bytesReadTotal << endl << endl;
		
		return true;
	}
	else
	{
		cout << "Sender: problem opening the file." << endl;
		fout << "Sender: problem opening the file." << endl;
		return false;
	}
}

bool putFile(int sock, char * filename, char * receiving_hostname, int server_number)
{
	Frame frame;
	Acknowledgment ack; ack.packetType = FRAME_ACK;
	long byteCount = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	int bytesReceived = 0, bytesWritten = 0, bytesWrittenTotal = 0;
	int sequenceNumber = server_number % 2;

	fout << "Receiver started on host " << receiving_hostname << endl;

	FILE * stream = fopen(filename, "w+b");

	if (stream != NULL)
	{
		while (1)
		{
			while (Receive(sock, &frame, nullptr, nullptr) != INCOMING) { ; }

			bytesReceived += sizeof(frame);

			if (frame.packetType == HANDSHAKE)
			{
				fout << "Receiver: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
			}
			else if (frame.packetType == FRAME)
			{
				fout << "Receiver: received frame " << (int)frame.seqWidth << endl;

				if ((int)frame.seqWidth != sequenceNumber)
				{
					ack.number = (int)frame.seqWidth;
					if (Send(sock, nullptr,nullptr,&ack) != sizeof(ack))
						return false;
					fout << "Receiver: sent ACK " << ack.number << " again" << endl;
					packetsSent++;
				}
				else
				{
					ack.number = (int)frame.seqWidth;
					if (Send(sock, nullptr,nullptr,&ack) != sizeof(ack))
						return false;
					fout << "Receiver: sent ack " << ack.number << endl;
					packetsSent++;
					packetsSentNeeded++;

					byteCount = frame.buffer_length;
					bytesWritten = fwrite(frame.buffer, sizeof(char), byteCount, stream);
					bytesWrittenTotal += bytesWritten;

					sequenceNumber = (sequenceNumber == 0 ? 1 : 0);

					if (frame.header == FINAL_DATA) { break; }
				}
			}
		}

		fclose(stream);
		cout << "Receiver: file transfer complete" << endl;
		cout << "Receiver: number of packets sent: " << packetsSent << endl;
		fout << "Receiver: file transfer complete" << endl;
		fout << "Receiver: number of packets sent: " << packetsSent << endl;
		fout << "Receiver: number of packets sent (needed): " << packetsSentNeeded << endl;
		fout << "Receiver: number of bytes received: " << bytesReceived << endl;
		fout << "Receiver: number of bytes written: " << bytesWrittenTotal << endl << endl;

		return true;
	}
	else
	{
		cout << "Receiver: problem opening the file." << endl;
		fout << "Receiver: problem opening the file." << endl;
		return false;
	}
}

void setHandshake(Request request)
{
	switch (request)
	{
	case GET:
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		if (fileExists(handshake.filename))
			handshake.type = ACK_CLIENT_NUM;
		else
		{
			handshake.type = FILE_NOT_EXIST;
			cout << "Server: requested file does not exist." << endl;
			fout << "Server: requested file does not exist." << endl;
		}
		break;

	case PUT:
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
		fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
		handshake.type = ACK_CLIENT_NUM;
		break;

	case LIST:
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		handshake.type = ACK_CLIENT_NUM;
		break;

	default:
		handshake.type = INVALID;
		cout << "Server: invalid request." << endl;
		fout << "Server: invalid request." << endl;
		break;
	}
}

void waitForHandshake()
{
	do {
		if (Send(sock, &handshake,nullptr,nullptr) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Server: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Server: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

	} while (Receive(sock, nullptr, &handshake, nullptr) != INCOMING || handshake.type != ACK_SERVER_NUM);
}

void HandshakeFactory(Handshake handshake)
{
	switch (handshake.request)
	{
	case GET:
		if (!getFile(sock, handshake.filename, server_name, handshake.client_number, false))
			err_sys("An error occurred while sending the file.");
		break;

	case LIST:
		strcpy(handshake.filename, "List/list.txt");
		if (!getFile(sock, handshake.filename, server_name, handshake.client_number, true))
			err_sys("An error occurred while sending the file.");
		break;

	case PUT:
		if (!putFile(sock, handshake.filename, server_name, handshake.server_number))
			err_sys("An error occurred while receiving the file.");
		break;

	default:
		break;
	}
}

void sockConnection()
{
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err_sys("socket() failed");

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(PORT1);
	sa_in_size = sizeof(sa_in);

	if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa)) < 0)
		err_sys("Socket binding error");
}

void handshakeType()
{

	if (handshake.type != ACK_CLIENT_NUM)
	{
		if (Send(sock, &handshake, nullptr,nullptr) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Server: sent error message to client. " << endl;
		fout << "Server: sent error message to client. " << endl;
	}
	else if (handshake.type == ACK_CLIENT_NUM)
	{
		srand((unsigned)time(NULL));
		random = rand() % 256;
		handshake.server_number = random;

		waitForHandshake();

		cout << "Server: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Server: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

		if (handshake.type == ACK_SERVER_NUM)
		{
			HandshakeFactory(handshake);
		}
		else
		{
			cout << "Handshake error!" << endl;
			fout << "Handshake error!" << endl;
		}
	}
}

unsigned long ResolveName(char name[])
{
	struct hostent *host;
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	return *((unsigned long *)host->h_addr_list[0]);
}

int main(void)
{
	timeouts.tv_sec = 0;
	timeouts.tv_usec = 300000;

	fout.open("server_log.txt");
	
	if (WSAStartup(0x0202, &wsadata) != 0)
	{
		WSACleanup();
		err_sys("Error in starting WSAStartup()\n");
	}

	if (gethostname(server_name, HOSTNAME_LENGTH) != 0)
		err_sys("Server gethostname() error.");

	printf("Server started on host [%s]\n", server_name);
	printf("Awaiting request for file transfer...\n", server_name);
	printf("\n");

	sockConnection();

	while (true)
	{
		while (Receive(sock, nullptr, &handshake, nullptr) != INCOMING || handshake.type != CLIENT_REQ) { ; }

		cout << "Server: received handshake C" << handshake.client_number << endl;
		fout << "Server: received handshake C" << handshake.client_number << endl;

		setHandshake(handshake.request);

		handshakeType();
	}

	fout.close();
	WSACleanup();

	return 0;
}
