#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map>
#include <string>
#include <fstream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <queue>
#include <condition_variable>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

unordered_map<string , string> database; // database storage for key -> value
shared_mutex dbMutex;
ofstream walStream;

//pool state
queue<SOCKET> taskQueue;
mutex queueMutex;
condition_variable cv;
bool stopPool = false;

void trimTrailing(string& str){
    while(!str.empty() && (str.back() == '\n' || str.back() == '\r')){
        str.pop_back();
    }
}

//WAL replay (one time heavy work)
void replayLogFile(){
    ifstream logFile("database.log");
    if(!logFile){
        cout << "STARTUP : No log file found. Starting with a fresh db.\n";
        return;
    }

    cout << "STARTUP: Found database.log. Replaying transactions to rebuild RAM...\n";
    string line;
    while (getline(logFile, line)) {
        trimTrailing(line);
        if (line.empty()) continue;

        if (line.rfind("SET ", 0) == 0) {
            size_t firstSpace = line.find(' ');
            size_t secondSpace = line.find(' ', firstSpace + 1);
            if (secondSpace != string::npos) {
                string key = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                string value = line.substr(secondSpace + 1);
                database[key] = value;
            }
        } else if (line.rfind("DEL ", 0) == 0) {
            string key = line.substr(4);
            database.erase(key);
        }
    }
    logFile.close();
}

//WAL append
void appendToLog(const string& transaction){
    if(walStream.is_open()){
        walStream << transaction << "\n";
        walStream.flush();
    }
}

void handleClient(SOCKET clientSocket){
    char buffer[4096]; //to hold incoming raw bytes

    while(true){
        ZeroMemory(&buffer , 4096);
        int bytesReceived = recv(clientSocket , buffer , 4096 , 0);

        if(bytesReceived == SOCKET_ERROR){
            cerr << "[Worker thread] Error in recv() with network error : " << WSAGetLastError << endl;
            break;
        }

        if(!bytesReceived){
            cout << "[Worker thread] Client disconnected gracefully.\n";
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

                {
                    unique_lock<shared_mutex> writeLock(dbMutex);
                    appendToLog("SET " + key + " " + value);
                    database[key] = value;
                }

                response = "Ok\n";
            }
        }

        else if(clientMessage.rfind("GET " , 0) == 0){
            string key = clientMessage.substr(4);

            {
                shared_lock<shared_mutex> readLock(dbMutex);

                auto it = database.find(key);
                if(it != database.end()){
                    response = it -> second + "\n";
                }else{
                    response = "ERR key not found.\n";
                }
            }
        }

        else if(clientMessage.rfind("EXISTS " , 0) == 0){
            string key = clientMessage.substr(7);

            {
                shared_lock<shared_mutex> readLock(dbMutex);
                auto it = database.find(key);
                response = (it != database.end()) ? "1\n" : "0\n";
            }
        }

        else if(clientMessage.rfind("DEL " , 0) == 0){
            string key = clientMessage.substr(4);

            {
                unique_lock<shared_mutex> writeLock(dbMutex);
                auto it = database.find(key);
                if (it != database.end()) {
                    appendToLog("DEL " + key);
                    database.erase(it);
                    response = "1\n";
                } else {
                    response = "0\n";
                }
            }
        }

        else if(clientMessage == "KEYS"){
            {
                shared_lock<shared_mutex> readLock(dbMutex);
                if (database.empty()) {
                    response = "(empty list)\n";
                } else {
                    string allKeys = "";
                    for (auto const& p : database) {
                        allKeys += p.first + " ";
                    }
                    if (!allKeys.empty()) allKeys.pop_back();
                    response = allKeys + "\n";
                }
            }
        }

        int sendResult = send(clientSocket , response.c_str() , response.size() , 0);
        if(sendResult == SOCKET_ERROR){
            cerr << "[Worker thread] send() failed with network error : " << WSAGetLastError() << endl;
            break;
        }
    }
    closesocket(clientSocket); //cleanup current client socket
    cout << "Session closed." << endl;
}

void poolWorkerLoop(){
    while(true){
        SOCKET clientSocket = INVALID_SOCKET;

        {
            unique_lock<mutex> lock(queueMutex);

            //puts thread to sleep until que is empty
            cv.wait(lock , []{return !taskQueue.empty() || stopPool;});

            if(stopPool && taskQueue.empty()) return;

            clientSocket = taskQueue.front();
            taskQueue.pop();
        }

        if(clientSocket != INVALID_SOCKET){
            handleClient(clientSocket);
        }
    }
}

int main(){

    replayLogFile();

    walStream.open("database.log" , ios :: app);
    if(!walStream){
        cerr << "CRITICAL : Coudn't open WAL storage.\n";
        return 1;
    }

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
    cout << "4. Server is now listening on port 8080\n";

    //5. Accept connection

    //thread pool intialization(cap thread usageto physical machine capabilities)
    const int THREAD_POOL_SIZE = 4;
    vector<thread> pool;
    for(int i = 0 ; i < THREAD_POOL_SIZE ; i++){
        pool.emplace_back(poolWorkerLoop);
    }

    cout << "Thread pool running with " << THREAD_POOL_SIZE << " static worker handles.\n\n";

    while(true){
        cout << "Waiting for client. \n";

        sockaddr_in clientHint;
        int clientSize = sizeof(clientHint);
        SOCKET clientSocket = accept(listenSocket , (sockaddr*)&clientHint , &clientSize);

        if(clientSocket == INVALID_SOCKET){
            cerr << "Accepting client connnection failed. Error : " << WSAGetLastError() << endl;
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        } else {
            {
                lock_guard<mutex> lock(queueMutex);
                taskQueue.push(clientSocket);
            }
        cv.notify_one(); //wake up exactly one thread
        }

        cout << "5. Client connnected successfully.\n";
    }
    
    //6. cleanup - just close sockets and turn off winsock
    if(walStream.is_open()) walStream.close();  
    closesocket(listenSocket);
    WSACleanup();
    cout << "Server shutdown cleanly.\n";

    return 0;
}