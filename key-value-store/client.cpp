#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

#pragma comment(lib , "ws2_32.lib")

int main(){
    //1. initialise winsock

    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2,2) , &wsaData) != 0){
        cerr << "Winsock initialisation failed.\n";
        return 1;
    }

    //2. client socket
    SOCKET clientSocket = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP);
    if(clientSocket == INVALID_SOCKET){
        cerr << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    //3. define servers address
    sockaddr_in serverHint;
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(8080);

    // inet_pton(AF_INET , "127.0.0.1" , &serverHint.sin_addr);  //ip to binary network format

    serverHint.sin_addr.s_addr = inet_addr("127.0.0.1");

    cout << "Connecting to server...\n";

    //4. connect to server
    int connectResult = connect(clientSocket , (sockaddr*)&serverHint , sizeof(serverHint));
    if(connectResult == SOCKET_ERROR){
        cerr << "Unable to connnect to server. Error : " << WSAGetLastError() << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    cout << "Connected to server successfully.\n";
    cout << "Type commands (e.g., 'SET name Alex' or 'GET name'). Type 'quit' to exit.\n\n";

    string userInput;
    char buffer[4096];

    do{
        cout << "kv_store> ";
        getline(cin , userInput);

        if(userInput == "quit"){
            break;
        }

        if(userInput.size() > 0){
            int sendResult = send(clientSocket , userInput.c_str() , userInput.size() , 0);

            if(sendResult != SOCKET_ERROR){
                ZeroMemory(&buffer , 4096);

                int bytesReceived = recv(clientSocket , buffer , 4096 , 0);

                if(bytesReceived > 0){
                    cout << "Server Response : " << string(buffer , bytesReceived);
                }
            }
        }
    }while(userInput.size() > 0);

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
