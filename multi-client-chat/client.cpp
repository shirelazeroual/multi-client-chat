
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

using namespace std;

/*
 * קודי צבעים לטרמינל (ANSI).
 * מאפשרים להדפיס הודעות בצבעים שונים כדי לשפר את חוויית המשתמש.
 */
#define RESET   "\033[0m"
#define GREEN   "\033[32m"
#define CYAN    "\033[36m"
#define YELLOW  "\033[33m"
#define RED     "\033[31m"

/*
 * socket של הלקוח – דרכו מתבצעת כל התקשורת עם השרת
 */
static int sockfd = -1;

/*
 * משתנה משותף לשני ה־threads (main + listener)
 * מסמן אם התוכנית עדיין רצה או שצריך להיסגר
 */
static atomic<bool> running(true);

/*
 * פונקציה זו רצה ב־thread נפרד.
 * התפקיד שלה הוא רק להקשיב לשרת ולקבל הודעות.
 * כל הודעה שמגיעה מהשרת מודפסת מיד למסך.
 */
void listen_to_server() {
    char buffer[2048];

    while (running) {
        // recv מחכה להודעה מהשרת
        ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

        // אם recv מחזיר 0 או ערך שלילי – החיבור נסגר או שיש שגיאה
        if (n <= 0) {
            cout << RED << "\n[Disconnected from server]\n" << RESET;
            running = false;
            break;
        }

        // סיום המחרוזת כדי שנוכל להדפיס אותה
        buffer[n] = '\0';

        // הדפסת הודעות מהשרת בצבע שונה
        cout << CYAN << buffer << RESET << flush;
    }
}

/*
 * פונקציית עזר לשליחת פקודה לשרת.
 * כל פקודה נשלחת כשורה שמסתיימת ב־newline (\n).
 */
bool send_cmd(const string& cmd) {
    string msg = cmd + "\n";

    // שליחת ההודעה לשרת
    ssize_t sent = send(sockfd, msg.c_str(), msg.size(), 0);

    // בדיקה בסיסית שהשליחה הצליחה
    return sent == (ssize_t)msg.size();
}

/*
 * הדפסת תפריט המשתמש.
 * המשתמש בוחר פעולה ע"י הקשת מספר בלבד,
 * ללא צורך להכיר פקודות או פרוטוקול.
 */
void print_menu() {
    cout << GREEN << "\n--- Chat Menu ---\n" << RESET;
    cout << YELLOW;
    cout << "1. Join room\n";
    cout << "2. Send message to room\n";
    cout << "3. Send private message\n";
    cout << "4. Send message to all\n";
    cout << "5. Show connected users\n";
    cout << "6. Exit\n";
    cout << RESET;
    cout << "Choose option (1-6): ";
}

int main(int argc, char* argv[]) {

    /*
     * בדיקת פרמטרים משורת הפקודה:
     * argv[1] – כתובת IP של השרת
     * argv[2] – מספר הפורט
     */
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <server_ip> <port>\n";
        return 1;
    }

    const char* server_ip = argv[1];
    int port = stoi(argv[2]);

    /*
     * יצירת socket TCP
     */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    /*
     * הגדרת כתובת השרת
     */
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // המרת כתובת IP ממחרוזת למבנה בינארי
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        cerr << "Invalid IP address\n";
        close(sockfd);
        return 1;
    }

    /*
     * התחברות לשרת
     */
    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    cout << GREEN << "Connected to chat server\n" << RESET;

    /*
     * יצירת thread שמקשיב להודעות מהשרת
     */
    thread listener(listen_to_server);

    /*
     * קביעת nickname ראשוני למשתמש
     */
    cout << "Enter nickname: ";
    string nick;
    getline(cin, nick);
    send_cmd("/nick " + nick);

    /*
     * לולאת העבודה הראשית של הלקוח
     * מציגה תפריט ומבצעת פעולות לפי בחירת המשתמש
     */
    while (running) {
        print_menu();

        string choice_str;
        getline(cin, choice_str);
        if (choice_str.empty()) continue;

        char choice = choice_str[0];

        if (choice == '1') {
            cout << "Room name: ";
            string room;
            getline(cin, room);
            send_cmd("/join " + room);
        }
        else if (choice == '2') {
            cout << "Message to room: ";
            string msg;
            getline(cin, msg);
            send_cmd("/room " + msg);
        }
        else if (choice == '3') {
            cout << "Send to user: ";
            string to;
            getline(cin, to);
            cout << "Message: ";
            string msg;
            getline(cin, msg);
            send_cmd("/msg " + to + " " + msg);
        }
        else if (choice == '4') {
            cout << "Message to all: ";
            string msg;
            getline(cin, msg);
            send_cmd("/all " + msg);
        }
        else if (choice == '5') {
            send_cmd("/who");
        }
        else if (choice == '6') {
            send_cmd("/quit");
            running = false;
        }
        else {
            cout << RED << "Invalid option\n" << RESET;
        }
    }

    /*
     * סגירה מסודרת של החיבור
     */
    close(sockfd);
    listener.join();

    return 0;
}