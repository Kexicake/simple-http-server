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
    QByteArray handleDbSelect(const QString &table, const QMap<QString, QString> &params);
    QByteArray handleDbInsert(const QString &table, const QMap<QString, QString> &params);
    QByteArray handleDbUpdate(const QString &table, const QMap<QString, QString> &params);
    QByteArray handleDbDelete(const QString &table, const QMap<QString, QString> &params);
protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    QSettings *m_settings;
    QString m_documentRoot;
    QString m_phpCgiPath;
    bool m_authEnabled;

    // БД
    QString m_dbConnectionStr;
    std::unique_ptr<pqxx::connection> m_dbConnection;

    // Обработчики
    QByteArray processRequest(const QString &method, const QString &path, const QMap<QString, QString> &headers, const QByteArray &body);
    QByteArray serveStaticFile(const QString &filePath);
    QByteArray executePhpScript(const QString &scriptPath, const QMap<QString, QString> &params, const QByteArray &postData);
    QByteArray serveApi(const QString &apiPath, const QString &method,
                       const QMap<QString, QString> &params,
                       const QJsonObject &jsonBody);

    // Вспомогательные методы
    QMap<QString, QString> parseQueryParams(const QString &query);
    QMap<QString, QString> parseFormUrlEncoded(const QByteArray &data);
    QByteArray createJsonResponse(const QJsonObject &json);
    QString jsonToString(const QJsonObject &jsonBody);
    QByteArray createErrorResponse(int code, const QString &message);
    bool checkAuthentication(const QMap<QString, QString> &headers);;
    bool validateCredentials(const QString &username, const QString &password);
    QByteArray createUnauthorizedResponse();
};

#endif // HTTPSERVER_H
