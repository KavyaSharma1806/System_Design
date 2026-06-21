#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map>
#include <string>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

int main(){

    //1. Initialize winsock

    WSADATA wsaData;
    int wsaStartupResult = WSAStartup(MAKEWORD(2 , 2) , &wsaData);
    if(wsaStartupResult != 0){
        cerr << "Winsock initialization failed. Error : " << wsaStartupResult << endl;
        return 1;
    }
    cout << "1. Winsock initialized succcessfully.\n";

    //2. create the socket IPv4-TCP

    SOCKET listenSocket = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP);
    if(listenSocket == INVALID_SOCKET){
        cerr << "Socket creation failed. Error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }
    cout << "2. Listening socket created.\n";

    //3. bind the socket to ip and port

    sockaddr_in serverHint;
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(8080);
    serverHint.sin_addr.S_un.S_addr = INADDR_ANY;

    int bindResult = bind(listenSocket , (sockaddr*)&serverHint , sizeof(serverHint ));
    if(bindResult == SOCKET_ERROR){
        cerr << "Binding failed. Error : " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
    cout << "3. Socket successfully bound to port 8080.\n";

    //4. listen

    int listenResult = listen(listenSocket , SOMAXCONN);
    if(listenResult == SOCKET_ERROR){
        cerr << "Listen failed. Error : " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
    cout << "4. Server is now listening on port 8080... (Waiting for client)\n";

    //5. Accept connection
    
    sockaddr_in clientHint;
    int clientSize = sizeof(clientHint);

    SOCKET clientSocket = accept(listenSocket , (sockaddr*)&clientHint , &clientSize);
    if(clientSocket == INVALID_SOCKET){
        cerr << "Accepting client connnection failed. Error : " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "5. Client connnected successfully.\n";

    unordered_map<string , string> database; // database storage for key -> value

    char buffer[4096]; //to hold incoming raw bytes

    while(true){
        ZeroMemory(&buffer , 4096);

        int bytesReceived = recv(clientSocket , buffer , 4096 , 0);

        if(bytesReceived == SOCKET_ERROR){
            cerr << "Error in recv(). Quiting.\n";
            break;
        }

        if(!bytesReceived){
            cout << "Client disconnected gracefully.\n";
            break;
        }

        //raw bytes to string
        string clientMessage(buffer , bytesReceived);
        cout << "Received raw command : " << clientMessage << endl;

        //Basic parsing
        //expected SET key value or GET  key value
        string response = "ERR unknown command\n";

        if(clientMessage.rfind("SET ", 0) == 0){
            size_t firstSpace = clientMessage.find(' ');
            size_t secondSpace = clientMessage.find(' ' , firstSpace + 1);

            if(secondSpace != string :: npos){
                string key = clientMessage.substr(firstSpace + 1 , secondSpace - firstSpace - 1);
                string value = clientMessage.substr(secondSpace + 1);

                database[key] = value;
                response = "Ok\n";
            }
        }
        else if(clientMessage.rfind("GET " , 0) == 0){
            string key = clientMessage.substr(4);

            if(database.find(key) != database.end()){
                response = database[key] + "\n";
            }else{
                response = "ERR key not found\n";
            }
        }

        send(clientSocket , response.c_str() , response.size() , 0);
    }

    //6. cleanup - just close sockets and turn off winsock
    
    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();
    cout << "Server shutdown cleanly.\n";

    return 0;
}