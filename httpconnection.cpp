#include "httpconnection.h"
#include <QDateTime>

HttpConnection::HttpConnection(QObject *parent) : QTcpSocket(parent)
{
    connect(this, &HttpConnection::readyRead, this, &HttpConnection::onReadyRead);
    connect(this, &HttpConnection::disconnected, this, &HttpConnection::disconnected);
}

void HttpConnection::onReadyRead()
{
    m_requestData.append(readAll());

    if (m_requestData.contains("\r\n\r\n")){
        parseRequest(m_requestData);
        m_requestData.clear();
    }
}

void HttpConnection::parseRequest(const QByteArray &data)
{


    QString requestStr = QString::fromUtf8(data);
    QStringList parts = requestStr.split("\r\n\r\n");
    QString headerPart = parts.value(0);
    QString body = parts.value(1);

    QStringList headers = headerPart.split("\r\n");
    QStringList requestLine = headers.takeFirst().split(' ');
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) return;

    QStringList requestList = lines.takeFirst().split(' ');
    if (requestList.size() < 2) return;

    QString method = requestList[0];
    QString path = requestList[1];

    QUrlQuery query;

    int queryPos = path.indexOf('?');
    if (queryPos != -1){
        query.setQuery(path.mid(queryPos + 1));
        path = path.left(queryPos);
    }


    if (path == "/post" && method == "POST"){
        sendResponse(("Post: " + body).toUtf8());
    }


    if (path == "/"){
        sendResponse("Test pass, response sended!");
    } else if (path == "/echo"){
        QString echoText = query.queryItemValue("text");
        if (echoText.isEmpty()) echoText = "NO TEXT PROVIDED";

        sendResponse(echoText.toUtf8());
    } else if (path == "/time"){
        //qInfo() << QDateTime::currentDateTimeUtc().toString();
        sendResponse(QDateTime::currentDateTimeUtc().toString().toUtf8());
    } else {
        sendResponse("404 Not Found", "text/plain", 404);
    }
}

void HttpConnection::sendResponse(const QByteArray &body, const QString &contentType, int statusCode)
{
    QByteArray response;
    response.append(QString("HTTP/1.1 %1 OK\r\n").arg(statusCode).toUtf8());
    response.append("Content-Type: " + contentType.toUtf8() + "\r\n");
    response.append("Connection: close\r\n");
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("\r\n");
    response.append(body);

    write(response);
    disconnectFromHost();
}
