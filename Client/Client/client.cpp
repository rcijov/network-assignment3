#include "client.h"
using namespace std;

//////////////////////////////////////////////////////////
//
//	Programmed by
//		Razvan Alin Cijov
//
//////////////////////////////////////////////////////////

bool fileExists(char * filename)
{
	std::ifstream infile(filename);
	return infile.good();
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

Package Receive(int sock, Frame * frame, Handshake * handshake, Acknowledgment * ack)
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	int bytes_recvd;
	int output = select(1, &readfds, NULL, NULL, &timeouts);
	switch (output)
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

bool putFile(int sock, char * filename, char * sending_hostname, int server_number)
{
	Frame frame; frame.packetType = FRAME;
	Acknowledgment ack; ack.number = -1;
	long byteFileSize = 0, byteCount = 0;
	int bytesSent = 0, bytesRead = 0, bytesReadTotal = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	bool firstPacket = true, finished = false;
	int sequenceNumber = server_number % 2;
	int tries; bool maxAttempts = false;

	fout << "Sender started on host " << sending_hostname << endl;

	FILE * stream = fopen(filename, "r+b");

	if (stream != NULL)
	{
		byteFileSize = fileSize(filename);
		while (1)
		{
			if (byteFileSize > MAX_FRAME_SIZE)
			{
				frame.header = (firstPacket ? INITIAL_DATA : DATA);
				byteCount = MAX_FRAME_SIZE;
			}
			else
			{
				byteCount = byteFileSize;
				finished = true;
				frame.header = FINAL_DATA;
			}

			byteFileSize -= MAX_FRAME_SIZE;

			bytesRead = fread(frame.buffer, sizeof(char), byteCount, stream);
			bytesReadTotal += bytesRead;
			frame.bufferLength = byteCount;
			frame.seqWidth = sequenceNumber;

			tries = 0;
			do {
				tries++;
				if (Send(sock, nullptr, &frame, nullptr) != sizeof(frame)){	return false;}
				packetsSent++;
				if (tries == 1){ packetsSentNeeded++; }
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
		fout << "Sender: number of bytes sent: " << bytesRead << endl;
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

bool getFile(int sock, char * filename, char * receiving_hostname, int client_number, bool list)
{
	Frame frame;
	Acknowledgment ack; ack.packetType = FRAME_ACK;
	long byteCount = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	int bytesReceived = 0, bytesWritten = 0, bytesWrittenTotal = 0;
	int sequenceNumber = client_number % 2;

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
				if (Send(sock, &handshake,nullptr,nullptr) != sizeof(handshake))
					err_sys("Error in sending packet.");
				fout << "Receiver: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl; 
			}
			else if (frame.packetType == FRAME)
			{
				fout << "Receiver: received frame " << (int)frame.seqWidth << endl;

				if ((int)frame.seqWidth != sequenceNumber)
				{
					ack.number = (int)frame.seqWidth;
					if (Send(sock,nullptr,nullptr, &ack) != sizeof(ack))
						return false;
					packetsSent++;
					fout << "Receiver: sent ACK " << ack.number << " again" << endl;
				}
				else
				{
					ack.number = (int)frame.seqWidth;
					if (Send(sock, nullptr,nullptr, &ack) != sizeof(ack))
						return false;
					fout << "Receiver: sent ACK " << ack.number << endl;
					packetsSent++;
					packetsSentNeeded++;

					byteCount = frame.bufferLength;
					bytesWritten = fwrite(frame.buffer, sizeof(char), byteCount, stream);
					bytesWrittenTotal += bytesWritten;

					if (list)
					{
						int i;
						for (i = 0; i < frame.bufferLength; i++)
						{
							cout << frame.buffer[i];
						}
					}
					cout << endl;

					sequenceNumber = (sequenceNumber == 0 ? 1 : 0);

					if (frame.header == FINAL_DATA)
						break;
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

void socketConnection()
{
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err_sys("socket() failed");

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(PORT1);

	if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa)) < 0)
		err_sys("Socket binding error");
}

bool menu()
{
	cout << "Enter command     : "; cin >> direction;
	if (strncmp(direction, "exit", 4) == 0){	return false;	}
	if (!strncmp(direction, "list", 4) == 0){ cout << "Enter file name   : "; cin >> filename; cout << endl; }
	cout << "Enter router host : "; cin >> remotehost;
	strcpy(handshake.hostname, hostname);
	strcpy(handshake.username, username);
	strcpy(handshake.filename, filename);
	return true;
}

void initiateConnection()
{
	struct hostent *rp;
	rp = gethostbyname(remotehost);
	memset(&sa_in, 0, sizeof(sa_in));
	memcpy(&sa_in.sin_addr, rp->h_addr, rp->h_length);
	sa_in.sin_family = rp->h_addrtype;
	sa_in.sin_port = htons(PORT2);
	sa_in_size = sizeof(sa_in);
	handshake.client_number = random;
	handshake.type = CLIENT_REQ;
	handshake.packetType = HANDSHAKE;
}

void setHandshake(char* direction)
{

	(strcmp(direction, "get") == 0) ? handshake.request = GET :
		((strcmp(direction, "put") == 0) ? handshake.request = PUT :
			((strcmp(direction, "list") == 0) ? handshake.request = LIST : 
				err_sys("Invalid direction. Use \"get\" or \"put\".")));
	
	if ((!fileExists(handshake.filename)) && (handshake.request == PUT))
	{
			err_sys("File does not exist on client side.");
	}
}

void HandshakeFactory(Request request)
{
	switch (request)
	{
	case GET:
		if (!getFile(sock, handshake.filename, hostname, handshake.client_number, false))
			err_sys("An error occurred while receiving the file.");
		break;
	case PUT:
		if (!putFile(sock, handshake.filename, hostname, handshake.server_number))
			err_sys("An error occurred while sending the file.");
		break;
	case LIST:
		strcpy(handshake.filename, "List/list.txt");
		if (!getFile(sock, handshake.filename, hostname, handshake.client_number, true))
			err_sys("An error occurred while receiving the file.");
		break;
	default:
		break;
	}
}

void setHandshakeType(HandshakeType handshaketype)
{
	switch (handshaketype)
	{
	case FILE_NOT_EXIST:
		cout << "File does not exist!" << endl;
		fout << "Client: requested file does not exist!" << endl;
		break;

	case INVALID:
		cout << "Invalid request." << endl;
		fout << "Client: invalid request." << endl;
		break;

	case ACK_CLIENT_NUM:
		cout << "Client: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Client: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

		handshake.type = ACK_SERVER_NUM;
		int sequence_number = handshake.server_number % 2;
		if (Send(sock,&handshake,nullptr,nullptr) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Client: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Client: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

		HandshakeFactory(handshake.request);
		break;
	}
}

void waitForHandshake()
{
	do
	{
		if (Send(sock, &handshake,nullptr,nullptr) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Client: sent handshake C" << handshake.client_number << endl;
		fout << "Client: sent handshake C" << handshake.client_number << endl;

	} while (Receive(sock, nullptr, &handshake, nullptr) != INCOMING);
}

unsigned long ResolveName(char name[])
{
	struct hostent *host;
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	return *((unsigned long *)host->h_addr_list[0]);
}

int main(int argc, char* argv[])
{
	// set timeout
	timeouts.tv_sec = 0;
	timeouts.tv_usec = 300000;

	// open log file
	fout.open("client_log.txt");
	
	char server[INPUT_LENGTH];
	unsigned long filename_length = (unsigned long)FILENAME_LENGTH;
	bool flag = true;

	if (WSAStartup(0x0202, &wsadata) != 0)
	{
		WSACleanup();
		err_sys("Error in starting WSAStartup()\n");
	}

	if (!GetUserName((LPWSTR)username, &filename_length))
		err_sys("Cannot get the user name");

	if (gethostname(hostname, (int)HOSTNAME_LENGTH) != 0)
		err_sys("Cannot get the host name");

	printf("User [%s] started client on host [%s]\n", username, hostname);
	printf("To exit, type \"exit\".\n\n");

	while (true)
	{

		socketConnection();

		srand((unsigned)time(NULL));
		random = rand() % 256;

		menu();

		if (flag)
		{
			initiateConnection();

			setHandshake(direction);

			waitForHandshake();

			setHandshakeType(handshake.type);

		}
		cout << "Closing client socket." << endl;
		fout << "Closing client socket." << endl;
		closesocket(sock);
	}

	fout.close();
	WSACleanup();
	return 0;
}
