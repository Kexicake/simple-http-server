#include "httpserver.h"
#include "httpconnection.h"

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent)
{
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    HttpConnection *connection = new HttpConnection(this);
    if(!connection->setSocketDescriptor(socketDescriptor)){
        connection->deleteLater();
        return;
    }

    connect(connection, &HttpConnection::disconnected, connection, &HttpConnection::deleteLater);
}
