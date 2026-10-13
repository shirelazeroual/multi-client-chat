
// שרת צ'אט מרובה לקוחות
// השרת עובד עם TCP sockets ותומך בחדרים, ניקים, הודעות פרטיות ולוג
// לכל לקוח נוצר thread נפרד
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <map>
#include <fstream>
#include <ctime>

using namespace std;

// מבני נתונים גלובליים לניהול לקוחות, ניקים וחדרים
// משתמשים ב-mutex כדי למנוע התנגשויות בין threads
vector<int> clients;
mutex clients_mutex;
map<int, string> nicks;
mutex nick_mutex;
map<string, vector<int>> rooms;   // room -> sockets
map<int, string> clientRoom;     // socket -> room
mutex room_mutex;
mutex log_mutex;
map<int, mutex*> send_mutexes;
mutex send_mutexes_mutex;

// קובץ לוג לרישום פעולות והודעות עם חותמת זמן
ofstream logFile("chat.log", ios::app);

void safeSend(int sock, const string& msg){
    send_mutexes_mutex.lock();
    if(send_mutexes.find(sock) == send_mutexes.end()){
        send_mutexes[sock] = new mutex();
    }
    mutex* m = send_mutexes[sock];
    send_mutexes_mutex.unlock();
    
    m->lock();
    send(sock, msg.c_str(), msg.size(), 0);
    m->unlock();
}

// פונקציה ששולחת הודעת מערכת לכל המשתמשים שנמצאים בחדר מסוים
void sendSystemToRoom(string room, string text){
    room_mutex.lock();
    vector<int> recipients;
    if(rooms.count(room)){
        recipients = rooms[room]; 
    }
    room_mutex.unlock();
    for(int c : recipients){
        safeSend(c, text);
    }
}

// פונקציה שרושמת אירועים חשובים לקובץ הלוג עם שעה
void logEvent(string text){
    time_t now = time(0);
    tm *t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "[%H:%M] ", t);

    log_mutex.lock();
    logFile << buf << text << endl;
    logFile.flush();
    log_mutex.unlock();
}


// פונקציה שמטפלת בלקוח אחד
// רצה בתוך thread נפרד ומקבלת פקודות והודעות מהלקוח
void handleClient(int client_socket){
    char buffer[1025];
    string partial = "";
    while(true){
        // קבלת הודעה מהלקוח דרך ה-socket
        int bytes = recv(client_socket, buffer, 1024, 0);

        // אם הלקוח התנתק – סוגרים את החיבור ומנקים את כל הנתונים שלו
        if(bytes <= 0){
            close(client_socket);
            clients_mutex.lock();
            clients.erase(
                remove(clients.begin(), clients.end(), client_socket),
                clients.end()
            );
            nick_mutex.lock();
            nicks.erase(client_socket);
            nick_mutex.unlock();
            clients_mutex.unlock();
            cout << "Client disconnected" << endl;
            room_mutex.lock();
            if(clientRoom.count(client_socket)){
                string room = clientRoom[client_socket];
                auto &v = rooms[room];
                v.erase(remove(v.begin(), v.end(), client_socket), v.end());
                clientRoom.erase(client_socket);
            }
            room_mutex.unlock();
            return;
        }

        // בדיקה אם ההודעה היא פקודה מיוחדת
        buffer[bytes] = '\0';
        partial += buffer;
        size_t pos;
        while((pos = partial.find('\n')) != string::npos){
            string msg = partial.substr(0, pos);
            partial = partial.substr(pos + 1);
            // פקודה שמחזירה ללקוח את רשימת כל המשתמשים המחוברים
            if(msg == "/who"){
                string list = "Users:\n";
                nick_mutex.lock();
                for(auto p : nicks){
                    list += p.second + "\n";
                }
                nick_mutex.unlock();
                safeSend(client_socket, list);
                continue;
            }
            // פקודה ליציאה מהשרת וניקוי הלקוח מכל המבנים
            if(msg == "/quit"){
                string room;
                bool inRoom = false;
                if(clientRoom.count(client_socket)){
                    room = clientRoom[client_socket];
                    inRoom = true;
                }
                string sender;
                nick_mutex.lock();
                sender = nicks.count(client_socket) ? nicks[client_socket] : "Guest";
                nick_mutex.unlock();

                string bye = "Bye\n";
                safeSend(client_socket, bye);
                close(client_socket);
                // הסרה מכל המבנים
                clients_mutex.lock();
                clients.erase(remove(clients.begin(), clients.end(), client_socket), clients.end());
                clients_mutex.unlock();
                nick_mutex.lock();
                nicks.erase(client_socket);
                nick_mutex.unlock();
                room_mutex.lock();
                if(clientRoom.count(client_socket)){
                    string room = clientRoom[client_socket];
                    auto &v = rooms[room];
                    v.erase(remove(v.begin(), v.end(), client_socket), v.end());
                    clientRoom.erase(client_socket);
                }
                room_mutex.unlock();
                cout << "Client quit\n";

                if(inRoom){
                    sendSystemToRoom(room, "SYS " + sender + " left the room\n");
                }
                logEvent(sender + " left room " + room);
                return;
            }

            // שליחת הודעה פרטית למשתמש לפי nickname
            if(msg.find("/msg ") == 0){
                int space1 = msg.find(' ', 5);
                if(space1 == string::npos) continue;
                string target = msg.substr(5, space1 - 5);
                string text = msg.substr(space1 + 1);
                int target_socket = -1;
                nick_mutex.lock();
                for(auto p : nicks){
                    if(p.second == target){
                        target_socket = p.first;
                        break;
                    }
                }
                nick_mutex.unlock();
                if(target_socket != -1){
                    string sender;
                    nick_mutex.lock();
                    sender = nicks[client_socket];
                    nick_mutex.unlock();
                    string out = "(Private) " + sender + ": " + text;
                    safeSend(target_socket, out);
                } else {
                    string err = "User not found\n";
                    safeSend(client_socket, err);
                }
                continue;
            }

            // כניסה לחדר צ'אט (אם היה בחדר קודם – יוצא ממנו)
            if(msg.find("/join ") == 0){
                string room = msg.substr(6);
                room_mutex.lock();
                // אם הלקוח היה בחדר קודם – נוציא אותו
                if(clientRoom.count(client_socket)){
                    string old = clientRoom[client_socket];
                    auto &v = rooms[old];
                    v.erase(remove(v.begin(), v.end(), client_socket), v.end());
                }
                rooms[room].push_back(client_socket);
                clientRoom[client_socket] = room;
                room_mutex.unlock();
                string ok = "Joined room " + room + "\n";
                safeSend(client_socket, ok);
                string sender;

                nick_mutex.lock();
                sender = nicks.count(client_socket) ? nicks[client_socket] : "Guest";
                nick_mutex.unlock();
                sendSystemToRoom(room, "SYS " + sender + " joined the room\n");
                logEvent(sender + " joined room " + room);
                continue;
            }

            // שליחת הודעה לכל המשתמשים שנמצאים באותו חדר
            if(msg.find("/room ") == 0){
                string text = msg.substr(6);
                string sender;
                nick_mutex.lock();
                sender = nicks.count(client_socket) ? nicks[client_socket] : "Guest";
                nick_mutex.unlock();
                string out = sender + ": " + text;
                logEvent(out);
                room_mutex.lock();
                if(clientRoom.count(client_socket)){
                    string room = clientRoom[client_socket];
                    for(int c : rooms[room]){
                        if(c != client_socket){
                            safeSend(c, out);
                        }
                    }
                } else {
                    string err = "You are not in a room\n";
                    safeSend(client_socket, err);
                }
                room_mutex.unlock();
                continue;
            }

            // שינוי nickname של הלקוח
            if(msg.find("/nick ") == 0){
                string nick = msg.substr(6);
                nick_mutex.lock();
                bool taken = false;
                for(auto& p : nicks){
                    if(p.second == nick && p.first != client_socket){
                        taken = true;
                        break;
                    }
                }
                if(taken){
                    nick_mutex.unlock();
                    string err = "ERR Nickname already taken\n";
                    safeSend(client_socket, err);
                    continue;
                }
                string old = nicks.count(client_socket) ? nicks[client_socket] : "Guest";
                nicks[client_socket] = nick;
                nick_mutex.unlock();
                string ok = "OK Nickname changed to " + nick + "\n";
                safeSend(client_socket, ok);
                if(clientRoom.count(client_socket)){
                    string room = clientRoom[client_socket];
                    sendSystemToRoom(room, "SYS " + old + " is now " + nick + "\n");
                }
                continue;
            }

            // שליחת הודעה לכל המחוברים
            if(msg.find("/all ") == 0){
                string text = msg.substr(5);
                string sender;
                nick_mutex.lock();
                sender = nicks.count(client_socket) ? nicks[client_socket] : "Guest";
                nick_mutex.unlock();
                clients_mutex.lock();
                vector<int> all_clients = clients;
                clients_mutex.unlock();
                string out = "[ALL] " + sender + ": " + text + "\n";
                for(int c : all_clients){
                    if(c != client_socket){
                        safeSend(c, out);
                    }
                }
                continue;
            } 

            // אם לא נשלחה פקודה – ההודעה נשלחת לכל הלקוחות המחוברים
            string sender;
            nick_mutex.lock();
            if(nicks.count(client_socket)){
                sender = nicks[client_socket];
            }else{
                sender = "Guest";
            }
            nick_mutex.unlock();
            string out = sender + ": " + msg;
            clients_mutex.lock();
            vector<int> all_clients = clients;
            clients_mutex.unlock();
            for(int c : all_clients){
                if(c != client_socket){
                    safeSend(c, out);
                }
            }
        }
    }   
}

// פונקציית main של השרת
// יוצרת socket, מאזינה ללקוחות ומפעילה thread לכל חיבור חדש
int main() {
    
    // יצירת Socket
    //Socket TCP לשרת
    int s_socket = socket(AF_INET, SOCK_STREAM, 0); //פוקציה(משפחה, סוג, פרוטוקול)
    int opt = 1;
    setsockopt(s_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if(s_socket < 0){
        cout << "Socket ERROR" << endl;
    }else{
        cout << "Socket OK" << endl;
    }

    //קושר לפורט 8080
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;


    // קישור ה-socket לפורט 8080
    int bind_socket = ::bind(s_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    if (bind_socket < 0) {
        cout << "Bind ERROR" << endl;
    } else {
        cout << "Bind OK" << endl;
    }

    // התחלת האזנה ללקוחות
    int listen_socket = listen(s_socket, 5);
    if (listen_socket < 0) {
        cout << "Listen ERROR" << endl;
    } else {
        cout << "Listen OK" << endl;
        cout << "Waiting for client..." << endl;
    }

    //קבלת לקוח
    sockaddr_in client_addr;
    socklen_t client_size = sizeof(client_addr);

    // קבלת חיבור מלקוח חדש
    while(true){
        int client_socket = accept(s_socket, (sockaddr*)&client_addr, &client_size);

        if(client_socket < 0){
            cout << "Accept Error" << endl;
            continue;

        }
        clients_mutex.lock();
        clients.push_back(client_socket);
        clients_mutex.unlock();

        cout << "Client connected" << endl;

        // יצירת thread נפרד לטיפול בלקוח
        thread t(handleClient, client_socket);
        t.detach();
    }
}