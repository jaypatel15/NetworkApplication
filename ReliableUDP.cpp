/*
	Reliability and Flow Control Example
	From "Networking for Game Programmers" - http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include<chrono>
// Adding file header in the "ReliableUDP.cpp" file as #include "Net.h"
#include "Net.h"
#define PACKET_SIZE 256
//#define SHOW_ACKS

using namespace std;
using namespace net;

const int ServerPort = 30000;
const int ClientPort = 30001;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f; 
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;
const int PacketSize = 256;

class FlowControl
{
public:

	FlowControl()
	{
		printf("flow control initialized\n");
		Reset();
	}

	void Reset()
	{
		mode = Bad;
		penalty_time = 4.0f;
		good_conditions_time = 0.0f;
		penalty_reduction_accumulator = 0.0f;
	}

	void Update(float deltaTime, float rtt)
	{
		const float RTT_Threshold = 250.0f;

		if (mode == Good)
		{
			if (rtt > RTT_Threshold)
			{
				printf("*** dropping to bad mode ***\n");
				mode = Bad;
				if (good_conditions_time < 10.0f && penalty_time < 60.0f)
				{
					penalty_time *= 2.0f;
					if (penalty_time > 60.0f)
						penalty_time = 60.0f;
					printf("penalty time increased to %.1f\n", penalty_time);
				}
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				return;
			}

			good_conditions_time += deltaTime;
			penalty_reduction_accumulator += deltaTime;

			if (penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f)
			{
				penalty_time /= 2.0f;
				if (penalty_time < 1.0f)
					penalty_time = 1.0f;
				printf("penalty time reduced to %.1f\n", penalty_time);
				penalty_reduction_accumulator = 0.0f;
			}
		}

		if (mode == Bad)
		{
			if (rtt <= RTT_Threshold)
				good_conditions_time += deltaTime;
			else
				good_conditions_time = 0.0f;

			if (good_conditions_time > penalty_time)
			{
				printf("*** upgrading to good mode ***\n");
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				mode = Good;
				return;
			}
		}
	}

	float GetSendRate()
	{
		return mode == Good ? 30.0f : 10.0f;
	}

private:

	enum Mode
	{
		Good,
		Bad
	};

	Mode mode;
	float penalty_time;
	float good_conditions_time;
	float penalty_reduction_accumulator;
};

// ----------------------------------------------

void sendFile(ReliableConnection* connection) 
{
	char filePath[256];
	printf("Enter the filename to send: ");
	scanf("%255s", filePath);

	const char* openMode = "rb";

	FILE* file = fopen(filePath, openMode);
	if (!file) 
	{
		perror("Error: Unable to open the file for sending");
		return;
	}

	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	rewind(file);

	char* filename = filePath;
	char* slash = NULL;

	for (char* p = filePath; *p != '\0'; p++) 
	{
		if (*p == '/' || *p == '\\') {
			slash = p;
		}
	}
	if (slash) 
	{
		filename = slash + 1;
	}

	uint32_t filenameLength = 0;
	while (filename[filenameLength] != '\0') 
	{
		filenameLength++;
	}

	printf("Sending filename: %s (Length: %d)\n", filename, filenameLength);

	unsigned char filenamePacket[256];
	filenamePacket[0] = 0x01;
	for (uint32_t i = 0; i < filenameLength; i++) 
	{
		filenamePacket[i + 1] = filename[i];
	}

	connection->SendPacket(filenamePacket, filenameLength + 1);

	unsigned char sizePacket[9];
	sizePacket[0] = 0x02; 
	unsigned char* sizePtr = (unsigned char*)&fileSize;
	for (size_t i = 0; i < sizeof(fileSize); i++) 
	{
		sizePacket[i + 1] = sizePtr[i];
	}

	connection->SendPacket(sizePacket, sizeof(sizePacket));

	unsigned char modePacket[2];
	modePacket[0] = 0x05; 
	modePacket[1] = 1;
	connection->SendPacket(modePacket, sizeof(modePacket));

	printf("Sending file: %s (%ld bytes)\n", filename, fileSize);

	char buffer[PACKET_SIZE];
	size_t bytesRead;
	while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		unsigned char contentPacket[PACKET_SIZE + 1];
		contentPacket[0] = 0x03; 
		for (size_t i = 0; i < bytesRead; i++)
		{
			contentPacket[i + 1] = (unsigned char)buffer[i];
		}
		connection->SendPacket(contentPacket, bytesRead + 1);
	}
	fclose(file);
	
	unsigned char eofPacket[1] = { 0x04 };
	connection->SendPacket(eofPacket, sizeof(eofPacket));

	printf("File transmission completed!\n");

	return;
}

void receiveFile(ReliableConnection* connection) 
{
	if (!connection->IsConnected()) return;

	char filename[256] = { 0 };
	FILE* file = nullptr;
	uint64_t expectedFileSize = 0, receivedFileSize = 0;
	bool receiving = false;
	unsigned char fileMode = 1;
	auto startTime = std::chrono::high_resolution_clock::now();

	while (true) 
	{
		unsigned char packet[PACKET_SIZE] = { 0 };
		int bytesRead = connection->ReceivePacket(packet, PACKET_SIZE);

		if (bytesRead <= 0) continue;

		if (packet[0] == 0x01) {
			int dataLength = bytesRead - 1;

			if (dataLength > 255)
				dataLength = 255;
			for (int i = 0; i < dataLength; i++) {
				filename[i] = (char)packet[i + 1];
			}

			filename[dataLength] = '\0';

			printf("Received filename: %s (Length: %d)\n", filename, bytesRead - 1);

			for (uint32_t i = 0; i < bytesRead - 1; i++)
			{
				if (filename[i] < 32 || filename[i] > 126) filename[i] = '_';
			}
		}
		else if (packet[0] == 0x02) 
		{ 
			expectedFileSize = 0;
			int copyBytes = sizeof(expectedFileSize);
			if (bytesRead - 1 < copyBytes)
				copyBytes = bytesRead - 1;
			unsigned char* sizePtr = (unsigned char*)&expectedFileSize;
			for (int i = 0; i < copyBytes; i++)
			{
				sizePtr[i] = packet[i + 1];
			}

		}
		else if (packet[0] == 0x05) 
		{ 
			fileMode = packet[1];

			const char* openMode = (fileMode == 1) ? "wb" : "w";
			file = fopen(filename, openMode);
			if (!file)
			{
				printf("Error: Unable to create the file: %s\n", filename);
				return;
			}
			printf("Receiving file: %s (%lu bytes)\n", filename, expectedFileSize);
			receiving = true;
		}
		else if (packet[0] == 0x03)
		{
			if (receiving && file) 
			{
				fwrite(packet + 1, 1, bytesRead - 1, file);
				receivedFileSize += bytesRead - 1;
			}

		}
		else if (packet[0] == 0x04)
		{
			if (file) fclose(file);
			printf("End of file received.\n");
			break;
		}
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsedTime = endTime - startTime;

	double transferSpeed = ((receivedFileSize * 8.0) / (elapsedTime.count() * 1'000'000));
	printf("File received successfully: %s (%lu bytes)\n", filename, receivedFileSize);
	printf("Transfer Time: %.6f seconds\n", elapsedTime.count());
	printf("Transfer Speed: %.6f Mbit/s\n", transferSpeed);

	return;
}

uint32_t calculateCRC32(const char* filePath)
{
	ifstream file(filePath, ios::binary);
	if (!file)
	{
		cerr << "Failed to open file: " << filePath << endl;
		return 0;
	}

	int32_t crc = static_cast<int32_t>(0xFFFFFFFF);
	char buffer[1024];
	while (file.read(buffer, sizeof(buffer))) 
	{
		for (int i = 0; i < file.gcount(); ++i)
		{
			crc ^= buffer[i];
			for (int j = 0; j < 8; ++j) {
				crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
			}
		}
	}
	return ~crc;
}
// ----------------------------------------------

int main(int argc, char* argv[])
{
	// parse command line

	enum Mode
	{
		Client,
		Server
	};
	// declaring and initializing mode with the any value(client and server)
	Mode mode = Server;
	Address address;

	if (argc >= 2)
	{
		int a, b, c, d;
# pragma warning(suppress : 4996)
		if (sscanf(argv[1], "%d.%d.%d.%d", &a, &b, &c, &d))
		{
			mode = Client;
			address = Address(a, b, c, d, ServerPort);
		}
	}

	// initialize

	if (!InitializeSockets())
	{
		printf("failed to initialize sockets\n");
		return 1;
	}

	ReliableConnection connection(ProtocolId, TimeOut);

	const int port = mode == Server ? ServerPort : ClientPort;

	if (!connection.Start(port))
	{
		printf("could not start connection on port %d\n", port);
		return 1;
	}

	if (mode == Client)
		connection.Connect(address);
	else
		connection.Listen();

	bool connected = false;
	float sendAccumulator = 0.0f;
	float statsAccumulator = 0.0f;

	FlowControl flowControl;

	while (true)
	{
		// update flow control

		if (connection.IsConnected())
			flowControl.Update(DeltaTime, connection.GetReliabilitySystem().GetRoundTripTime() * 1000.0f);

		const float sendRate = flowControl.GetSendRate();

		// detect changes in connection state

		if (mode == Server && connected && !connection.IsConnected())
		{
			flowControl.Reset();
			printf("reset flow control\n");
			connected = false;
		}

		if (!connected && connection.IsConnected())
		{
			printf("client connected to server\n");
			connected = true;
		}

		if (!connected && connection.ConnectFailed())
		{
			printf("connection failed\n");
			break;
		}

		if (mode == Client) 
		{
			sendFile(&connection);
		}

		if (mode == Server) 
		{
			receiveFile(&connection);
		}

		// send and receive packets

		sendAccumulator += DeltaTime;

		// This logic indicates load the file , stores the file and break the file in pieces  after which the packets will be sent.
		while (sendAccumulator > 1.0f / sendRate)
		{
			unsigned char packet[PacketSize];
			memset(packet, 0, sizeof(packet));
			connection.SendPacket(packet, sizeof(packet));
			sendAccumulator -= 1.0f / sendRate;
		}

		// This logic continously receive the packet in a loop 
		// then it read the data and store the packet.
		while (true)
		{
			unsigned char packet[256];
			int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
			if (bytes_read == 0)
				break;
		}

		// show packets that were acked this frame

        #ifdef SHOW_ACKS
		unsigned int* acks = NULL;
		int ack_count = 0;
		connection.GetReliabilitySystem().GetAcks(&acks, ack_count);
		if (ack_count > 0)
		{
			printf("acks: %d", acks[0]);
			for (int i = 1; i < ack_count; ++i)
				printf(",%d", acks[i]);
			printf("\n");
		}
        #endif

		// update connection

		connection.Update(DeltaTime);

		// show connection stats

		statsAccumulator += DeltaTime;

		while (statsAccumulator >= 0.25f && connection.IsConnected())
		{
#ifdef SHOW_STATS
			float rtt = connection.GetReliabilitySystem().GetRoundTripTime();

			unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
			unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
			unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();

			float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
			float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();

			printf("rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n",
				rtt * 1000.0f, sent_packets, acked_packets, lost_packets,
				sent_packets > 0.0f ? (float)lost_packets / (float)sent_packets * 100.0f : 0.0f,
				sent_bandwidth, acked_bandwidth);

#endif
			statsAccumulator -= 0.25f;
		}

		net::wait(DeltaTime);
	}

	ShutdownSockets();

	return 0;
}
