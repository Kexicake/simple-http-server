#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QTcpServer>
#include <QObject>
#include <QTcpServer>
#include <QMap>
#include <QString>
#include <QProcess>
#include <QSettings>
#include <pqxx/pqxx>

class HttpConnection;

class HttpServer : public QTcpServer
{
        Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();

    bool startServer(quint16 port = 8080);
    void stopServer();


    // Конфиг
    void setDocumentRoot(const QString &path);
    void setPhpCgiPath(const QString &path);
    void configureDatabase(const QString &connStr);

    // API
    QByteArray handleDbRequest(const QString &table, const QMap<QString, QString> &params, const QString &method);
protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    QString m_documentRoot = "/home/kexicake/projects/simple-http-server/www";
    QString m_phpCgiPath = "/usr/bin/php-cgi";
    QSettings *m_settings;

    // БД
    QString m_dbConnectionStr;
    std::unique_ptr<pqxx::connection> m_dbConnection;

    // Обработчики
    QByteArray processRequest(const QString &method, const QString &path, const QMap<QString, QString> &headers, const QByteArray &body);
    QByteArray serveStaticFile(const QString &filePath);
    QByteArray executePhpScript(const QString &scriptPath, const QMap<QString, QString> &params, const QByteArray &postData);
    QByteArray serveApi(const QString &apiPath, const QMap<QString, QString> &params, const QByteArray &postData);

    // Вспомогательные методы
    QMap<QString, QString> parseQueryParams(const QString &query);
    QByteArray createJsonResponse(const QJsonObject &json);
    QByteArray createErrorResponse(int code, const QString &message);
    bool checkAuthentication(const QMap<QString, QString> &headers);
};

#endif // HTTPSERVER_H
