#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QTcpServer>

class HttpConnection;

class HttpServer : public QTcpServer
{
        Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};

#endif // HTTPSERVER_H
