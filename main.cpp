#include <QCoreApplication>
#include "httpserver.h"


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    HttpServer server;
    if (!server.listen(QHostAddress::Any, 8080)){
        qCritical() << "Failed to start http server:" << server.errorString();
        return 1;
    }

    qInfo() << "Server rubbing on http://localhost:8080";
    return a.exec();
}
