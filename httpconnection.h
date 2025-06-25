#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <QTcpSocket>
#include <QUrlQuery>


class HttpConnection : public QTcpSocket
{
    Q_OBJECT
public:
    explicit HttpConnection(QObject *parent = nullptr);

signals:
    void disconnected();

private slots:
    void onReadyRead();

private:
    void parseRequest(const QByteArray &data);
    void sendResponse(const QByteArray &body, const QString &contentType = "text/plain", int statusCode = 200);

    QByteArray m_requestData;
};

#endif // HTTPCONNECTION_H
