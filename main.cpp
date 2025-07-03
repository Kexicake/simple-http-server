#include <QCoreApplication>
#include "httpserver.h"


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    HttpServer server;
    if (!server.startServer(8080)) return 1;


    return a.exec();
}
