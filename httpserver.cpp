#include "httpserver.h"
#include <QTcpSocket>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QCoreApplication>
#include <QEventLoop>
#include <QtCore/QString>

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent),
    m_settings(new QSettings("/home/kexicake/projects/simple-http-server/http_server.ini", QSettings::IniFormat)),
    m_documentRoot("/home/kexicake/projects/simple-http-server/www"),
    m_phpCgiPath("/usr/bin/php-cgi"),
    m_authEnabled(true)
{
    // Проверка доступности файла конфига
    if (!QFile::exists(m_settings->fileName())) {
        qWarning() << "Config file not found:" << m_settings->fileName();
        // Создаем файл с дефолтными значениями
        m_settings->setValue("server/document_root", m_documentRoot);
        m_settings->setValue("php/cgi_path", m_phpCgiPath);
        m_settings->setValue("auth/enabled", m_authEnabled);
        m_settings->setValue("database/connection_string",
                           "dbname=simple_http_db user=postgres password=postgres host=localhost port=5432");
        m_settings->sync();
    }

    // Теперь загружаем настройки
    setDocumentRoot(m_settings->value("server/document_root", m_documentRoot).toString());
    setPhpCgiPath(m_settings->value("php/cgi_path", m_phpCgiPath).toString());
    m_authEnabled = m_settings->value("auth/enabled", true).toBool();

    // Настройка БД
    QString dbConnStr = m_settings->value("database/connection_string",
        "dbname=simple_http_db user=postgres password=postgres host=localhost port=5432").toString();
    configureDatabase(dbConnStr);
}
HttpServer::~HttpServer()
{
    stopServer();
    delete m_settings;
}

bool HttpServer::startServer(quint16 port)
{
    if (!listen(QHostAddress::Any, port)){
        qWarning() << "Failed to start server:" << errorString();
        return false;
    }

    qInfo() << "Server started on port" << port;
    qInfo() << "Server dir:" << QCoreApplication::applicationDirPath();
    qInfo() << "Document root:" << m_documentRoot;
    qInfo() << "PHP CHI root:" << m_phpCgiPath;
    qDebug() << "Config file location:" << m_settings->fileName();
    return true;
}

void HttpServer::stopServer()
{
    if (isListening()){
        close();
        qInfo() << "Server stopped";
    }
}

void HttpServer::setDocumentRoot(const QString &path)
{
    m_documentRoot = path + "/www";
    m_settings->setValue("server/document_root", path);
}

void HttpServer::setPhpCgiPath(const QString &path)
{
    m_phpCgiPath = path;  // Используем переданный параметр
    m_settings->setValue("php/cgi_path", m_phpCgiPath);
}

void HttpServer::configureDatabase(const QString &connStr)
{
    m_dbConnectionStr = connStr;
    m_settings->setValue("database.connection_string", m_dbConnectionStr);

    try {
        m_dbConnection = std::make_unique<pqxx::connection>(m_dbConnectionStr.toStdString());
        qInfo() << "Connected to PostgreSQL database:" << QString::fromStdString(m_dbConnection->dbname());
    } catch (const std::exception &e) {
        qWarning() << "Database connection error:" << e.what();
    }
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        // Чтение запроса
        QByteArray requestData = socket->readAll();

        // Парсинг запроса
        QString requestStr = QString::fromUtf8(requestData);

        QStringList lines = requestStr.split("\r\n");
//        for (int i = 0; i < lines.size(); ++i)
//            qInfo() << lines[i];
        if (lines.isEmpty()) {
            socket->write(createErrorResponse(400, "Bad Request"));
            socket->disconnectFromHost();
            return;
        }

        // Метод путь и версия http, первая строка запроса короч
        QStringList requestLine = lines[0].split(' ');
        if (requestLine.size() < 3) {
            socket->write(createErrorResponse(400, "Bad Request"));
            socket->disconnectFromHost();
            return;
        }

        QString method = requestLine[0];
        QString path = requestLine[1];
        QString httpVersion = requestLine[2];

        // Парсинг заголовков
        QMap<QString, QString> headers;
        int i = 1;
        for (; i < lines.size(); ++i) {
            if (lines[i].isEmpty()) break;

            int colonPos = lines[i].indexOf(':');
            if (colonPos > 0) {
                QString key = lines[i].left(colonPos).trimmed();
                QString value = lines[i].mid(colonPos).trimmed();
                headers[key.toLower()] = value;
            }
        }

        // Body - тело запроса если есть
        QByteArray body;
        int emptyLineIndex = requestData.indexOf("\r\n\r\n");
        if (emptyLineIndex != -1) {
            body = requestData.mid(emptyLineIndex + 4); // +4 для пропуска \r\n\r\n
        }

        // Сама обработка запроса
        QByteArray response = processRequest(method, path, headers, body);
        socket->write(response);
        socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    if (!socket->setSocketDescriptor(socketDescriptor)) {
        delete socket;
        return;
    }
}
QMap<QString, QString> HttpServer::parseFormUrlEncoded(const QByteArray &data) {
    QMap<QString, QString> result;
    QUrlQuery query(QString::fromUtf8(data));
    for (auto &item : query.queryItems()) {
        result[item.first] = item.second;
    }
    return result;
}
QString HttpServer::jsonToString(const QJsonObject &jsonBody) {
    QJsonDocument doc(jsonBody);
    return doc.toJson(QJsonDocument::Compact);
}
QByteArray HttpServer::processRequest(const QString &method, const QString &path, const QMap<QString, QString> &headers, const QByteArray &body)
{
    // Разбор URL
    QUrl url(path);
    QString cleanPath = url.path();

    // Проверка аутентификации для API
//    if (cleanPath.startsWith("/api/") && !checkAuthentication(headers)) {
//        return createErrorResponse(401, "Unauthorized");
//    }

    // Обработка API запросов
    if (cleanPath.startsWith("/api/")) {
        QString apiPath = cleanPath.mid(5); // Убираем "/api/"
        // Определение Content-Type
            QString contentType = headers.value("content-type", "application/x-www-form-urlencoded");

            // Парсинг параметров в зависимости от метода и Content-Type
            QMap<QString, QString> params;
            QJsonObject jsonBody;

            if (method == "GET") {
                params = parseQueryParams(url.query());
            }
            else if (contentType.contains("application/json")) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
                if (parseError.error == QJsonParseError::NoError) {
                    jsonBody = doc.object();
                } else {
                    return createErrorResponse(400, "Invalid JSON: " + parseError.errorString());
                }
            }
            else if (contentType.contains("application/x-www-form-urlencoded")) {
                params = parseFormUrlEncoded(body);
            }

            // Логирование запроса (для отладки)
            qDebug() << "API Request:" << method << apiPath << "\n" << "Params:" << params << "\n" << "JSON:" << jsonToString(jsonBody);

            // Обработка API
            return serveApi(apiPath, method, params, jsonBody);
    }

    // Определение файла для обслуживания
    QString filePath = m_documentRoot + cleanPath;
    if (filePath.endsWith('/')) {
        filePath += "index.html";
    }

    QFileInfo fileInfo(filePath);

    // Проверка существования файла
    if (!fileInfo.exists()) {
        qDebug() << "Method:" << method << "\n" << "Filepath:" << filePath << " Not Found";
        return createErrorResponse(404, "Not Found");
    }

    // Проверка на директорию
    if (fileInfo.isDir()) {
        filePath += "/index.html";
        fileInfo.setFile(filePath);

        if (!fileInfo.exists()) {
            return createErrorResponse(403, "Forbidden");
        }
    }

    // Обработка PHP скриптов
    if (fileInfo.suffix().toLower() == "php") {
        QMap<QString, QString> params = parseQueryParams(url.query());
        return executePhpScript(filePath, params, body);
    }

    // Отдача статического файла
    return serveStaticFile(filePath);
}

QByteArray HttpServer::serveStaticFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return createErrorResponse(403, "Forbidden");
    }

    QByteArray content = file.readAll();
    file.close();

    // Определение MIME-типа
    QString mimeType = "text/plain";
    QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == "html") mimeType = "text/html";
    else if (suffix == "css") mimeType = "text/css";
    else if (suffix == "js") mimeType = "application/javascript";
    else if (suffix == "json") mimeType = "application/json";
    else if (suffix == "jpg" || suffix == "jpeg") mimeType = "image/jpeg";
    else if (suffix == "png") mimeType = "image/png";
    else if (suffix == "gif") mimeType = "image/gif";
    else if (suffix == "svg") mimeType = "image/svg+xml";

    // Формирование ответа
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: " + mimeType.toUtf8() + "\r\n");
    response.append("Content-Length: " + QByteArray::number(content.size()) + "\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(content);

    return response;
}

QByteArray HttpServer::executePhpScript(const QString &scriptPath, const QMap<QString, QString> &params, const QByteArray &postData)
{
    QProcess phpProcess;
    QFileInfo phpFile(m_phpCgiPath);
    if (!phpFile.exists() || !phpFile.isExecutable()) {
        qWarning() << "Invalid PHP-CGI path:" << m_phpCgiPath;
        return createErrorResponse(500, "PHP-CGI path is invalid or not executable");
    }
    QFileInfo scriptFile(scriptPath);
    if (!scriptFile.exists() || !scriptFile.isReadable()) {
        qWarning() << "Script file missing or unreadable:" << scriptPath;
        return createErrorResponse(500, "Script file missing or unreadable");
    }
    // Установка переменных окружения CGI
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GATEWAY_INTERFACE", "CGI/1.1");
    env.insert("REQUEST_METHOD", postData.isEmpty() ? "GET" : "POST");
    env.insert("SCRIPT_FILENAME", scriptPath);
    env.insert("SCRIPT_NAME", QFileInfo(scriptPath).fileName());
    env.insert("REDIRECT_STATUS", "200");
    env.insert("SERVER_PROTOCOL", "HTTP/1.1");
    env.insert("CONTENT_TYPE", "application/x-www-form-urlencoded");
    if (!postData.isEmpty())
        env.insert("CONTENT_LENGTH", QString::number(postData.size()));

    // Добавление GET-параметров
    QString queryString;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!queryString.isEmpty()) queryString += "&";
        queryString += QUrl::toPercentEncoding(it.key()) + "=" + QUrl::toPercentEncoding(it.value());
    }
    env.insert("QUERY_STRING", queryString);

    phpProcess.setProcessEnvironment(env);

    // Запуск PHP-CGI
    phpProcess.start(m_phpCgiPath, QStringList() << "-f" << scriptPath);
    if (!phpProcess.waitForStarted()) {
        qWarning() << "Failed to start PHP CGI process.";
        return createErrorResponse(500, "Failed to start PHP CGI process");
    }

    // Отправка POST-данных
    if (!postData.isEmpty()) {
        phpProcess.write(postData);
    }
    phpProcess.closeWriteChannel();

    if (!phpProcess.waitForFinished()) {
        qWarning() << "PHP CGI did not finish properly.";
        return createErrorResponse(500, "PHP CGI did not finish properly");
    }

    QByteArray stdOut = phpProcess.readAllStandardOutput();
    QByteArray stdErr = phpProcess.readAllStandardError();

    if (!stdErr.isEmpty()) {
        qWarning() << "PHP CGI stderr:" << stdErr;
    }

    if (stdOut.isEmpty()) {
        return createErrorResponse(500, "Empty response from PHP CGI:\n" + stdErr);
    }

    // Проверка наличия заголовков
    int headerEnd = stdOut.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
        return createErrorResponse(500, "Invalid response from PHP CGI:\n" + stdOut + "\n" + stdErr);
    }

    return stdOut;
}

//QMap<QString, QString> HttpServer::jsonToMap(const QJsonObject &json) {
//    QMap<QString, QString> result;
//    for (auto it = json.begin(); it != json.end(); ++it) {
//        result[it.key()] = it.value().toString();
//    }
//    return result;
//}

QByteArray HttpServer::serveApi(const QString &apiPath, const QString &method,
                               const QMap<QString, QString> &params,
                               const QJsonObject &jsonBody)
{
    // Разделяем путь API на компоненты
    QStringList pathParts = apiPath.split('/', QString::SkipEmptyParts);
    if (pathParts.isEmpty()) {
        return createErrorResponse(400, "Invalid API path");
    }

    QString resource = pathParts[0];
    QString action = pathParts.size() > 1 ? pathParts[1] : "";
    QString identifier = pathParts.size() > 2 ? pathParts[2] : "";

    // Обработка запросов к базе данных
    if (resource == "db") {
        if (!m_dbConnection) {
            return createErrorResponse(503, "Database not available");
        }

        if (action.isEmpty()) {
            return createErrorResponse(400, "Database table not specified");
        }

        QString table = action;
        QMap<QString, QString> allParams = params;

        // Добавляем параметры из JSON тела, если есть
        if (!jsonBody.isEmpty()) {
            for (auto it = jsonBody.begin(); it != jsonBody.end(); ++it) {
                allParams[it.key()] = it.value().toString();
            }
        }

        try {
            if (method == "GET") {
                return handleDbSelect(table, allParams);
            }
            else if (method == "POST") {
                return handleDbInsert(table, allParams);
            }
            else if (method == "PUT") {
                if (identifier.isEmpty() && !allParams.contains("id")) {
                    return createErrorResponse(400, "ID not specified for update");
                }
                return handleDbUpdate(table, allParams);
            }
            else if (method == "DELETE") {
                if (identifier.isEmpty() && !allParams.contains("id")) {
                    return createErrorResponse(400, "ID not specified for deletion");
                }
                return handleDbDelete(table, allParams);
            }
            else {
                return createErrorResponse(405, "Method not allowed");
            }
        } catch (const std::exception &e) {
            return createErrorResponse(500, QString("Database error: ") + e.what());
        }
    }

    return createErrorResponse(404, "API endpoint not found");
}
QByteArray HttpServer::handleDbSelect(const QString &table, const QMap<QString, QString> &params)
{
    pqxx::work txn(*m_dbConnection);
    QJsonObject result;

    std::string query = "SELECT * FROM " + table.toStdString();

    if (!params.isEmpty()) {
        query += " WHERE ";
        bool first = true;
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (!first) query += " AND ";
            first = false;
            query += it.key().toStdString() + " = " + txn.quote(it.value().toStdString());
        }
    }

    if (params.contains("_order")) {
        query += " ORDER BY " + params["_order"].toStdString();
    }

    if (params.contains("_limit")) {
        query += " LIMIT " + params["_limit"].toStdString();
    }

    pqxx::result res = txn.exec(query);

    QJsonArray items;
    for (auto row : res) {
        QJsonObject item;
        for (auto field : row) {
            item[QString::fromStdString(field.name())] = QString::fromStdString(field.c_str());
        }
        items.append(item);
    }

    result["status"] = "success";
    result["data"] = items;
    txn.commit();
    return createJsonResponse(result);
}

QByteArray HttpServer::handleDbInsert(const QString &table, const QMap<QString, QString> &params)
{
    if (params.isEmpty()) {
        return createErrorResponse(400, "No data provided");
    }

    pqxx::work txn(*m_dbConnection);
    QJsonObject result;

    std::string columns, values;
    bool first = true;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.key().startsWith("_")) continue; // Пропускаем служебные параметры
        if (!first) {
            columns += ", ";
            values += ", ";
        }
        first = false;
        columns += it.key().toStdString();
        values += txn.quote(it.value().toStdString());
    }

    std::string query = "INSERT INTO " + table.toStdString() +
                       " (" + columns + ") VALUES (" + values + ") RETURNING id";

    pqxx::result res = txn.exec(query);
    txn.commit();

    if (!res.empty()) {
        result["status"] = "success";
        result["id"] = res[0][0].as<int>();
    } else {
        result["status"] = "error";
        result["message"] = "Insert failed";
    }

    return createJsonResponse(result);
}

QByteArray HttpServer::handleDbUpdate(const QString &table, const QMap<QString, QString> &params)
{
    if (params.isEmpty()) {
        return createErrorResponse(400, "No data provided");
    }

    if (!params.contains("id")) {
        return createErrorResponse(400, "ID not specified");
    }

    pqxx::work txn(*m_dbConnection);
    QJsonObject result;

    std::string setClause;
    bool first = true;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.key() == "id" || it.key().startsWith("_")) continue;
        if (!first) setClause += ", ";
        first = false;
        setClause += it.key().toStdString() + " = " + txn.quote(it.value().toStdString());
    }

    if (setClause.empty()) {
        return createErrorResponse(400, "No fields to update");
    }

    std::string query = "UPDATE " + table.toStdString() +
                       " SET " + setClause +
                       " WHERE id = " + txn.quote(params["id"].toStdString()) +
                       " RETURNING id";

    pqxx::result res = txn.exec(query);
    txn.commit();

    if (!res.empty()) {
        result["status"] = "success";
        result["updated_id"] = res[0][0].as<int>();
    } else {
        result["status"] = "error";
        result["message"] = "Update failed - record not found";
    }

    return createJsonResponse(result);
}

QByteArray HttpServer::handleDbDelete(const QString &table, const QMap<QString, QString> &params)
{
    if (!params.contains("id")) {
        return createErrorResponse(400, "ID not specified");
    }

    pqxx::work txn(*m_dbConnection);
    QJsonObject result;

    std::string query = "DELETE FROM " + table.toStdString() +
                       " WHERE id = " + txn.quote(params["id"].toStdString()) +
                       " RETURNING id";

    pqxx::result res = txn.exec(query);
    txn.commit();

    if (!res.empty()) {
        result["status"] = "success";
        result["deleted_id"] = res[0][0].as<int>();
    } else {
        result["status"] = "error";
        result["message"] = "Delete failed - record not found";
    }

    return createJsonResponse(result);
}

QMap<QString, QString> HttpServer::parseQueryParams(const QString &query)
{
    QMap<QString, QString> params;
    QUrlQuery urlQuery(query);

    for (auto &item : urlQuery.queryItems()) {
        params[item.first] = item.second;
    }

    return params;
}
QByteArray HttpServer::createJsonResponse(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson();

    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json\r\n");
    response.append("Content-Length: " + QByteArray::number(jsonData.size()) + "\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(jsonData);

    return response;
}

QByteArray HttpServer::createErrorResponse(int code, const QString &message)
{
    QJsonObject json;
    json["status"] = "error";
    json["code"] = code;
    json["message"] = message;

    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson();

    QString statusLine;
    switch (code) {
        case 400: statusLine = "HTTP/1.1 400 Bad Request"; break;
        case 401: statusLine = "HTTP/1.1 401 Unauthorized"; break;
        case 403: statusLine = "HTTP/1.1 403 Forbidden"; break;
        case 404: statusLine = "HTTP/1.1 404 Not Found"; break;
        case 500: statusLine = "HTTP/1.1 500 Internal Server Error"; break;
        default: statusLine = "HTTP/1.1 " + QString::number(code) + " Error"; break;
    }

    QByteArray response;
    response.append(statusLine.toUtf8() + "\r\n");
    response.append("Content-Type: application/json\r\n");
    response.append("Content-Length: " + QByteArray::number(jsonData.size()) + "\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(jsonData);

    return response;
}
bool HttpServer::checkAuthentication(const QMap<QString, QString> &headers)
{
    QString authHeader = headers.value("authorization");
    if (!authHeader.startsWith("Basic ")) {
        qDebug() << "Invalid auth header format";
        return false;
    }

    QByteArray authData = QByteArray::fromBase64(authHeader.mid(6).toUtf8());
    QString authStr = QString::fromUtf8(authData);
    QStringList parts = authStr.split(':');

    if (parts.size() != 2) {
        qDebug() << "Invalid auth data format";
        return false;
    }

    QString username = parts[0];
    QString password = parts[1];

    // Проверка учетных данных
    if (!m_dbConnection) {
        qWarning() << "Database connection not available for auth";
        return false;
    }

    try {
        pqxx::work txn(*m_dbConnection);
        std::string query = "SELECT password FROM users WHERE username = " +
                          txn.quote(username.toStdString());

        pqxx::result res = txn.exec(query);

        if (res.empty()) {
            qDebug() << "User not found:" << username;
            return false;
        }

        std::string dbPassword = res[0][0].as<std::string>();
        bool authenticated = (password == QString::fromStdString(dbPassword));

        if (!authenticated) {
            qDebug() << "Invalid password for user:" << username;
        }

        return authenticated;

    } catch (const std::exception &e) {
        qWarning() << "Auth error:" << e.what();
        return false;
    }
}

bool HttpServer::validateCredentials(const QString &username, const QString &password)
{
    if (!m_dbConnection) {
        qWarning() << "Database not connected for auth validation";
        return false;
    }

    try {
        pqxx::work txn(*m_dbConnection);
        std::string query = "SELECT password FROM users WHERE username = " +
                          txn.quote(username.toStdString());

        pqxx::result res = txn.exec(query);

        if (res.empty()) {
            return false; // Пользователь не найден
        }

        std::string dbPassword = res[0][0].as<std::string>();
        return (password == QString::fromStdString(dbPassword));

    } catch (const std::exception &e) {
        qWarning() << "Auth validation error:" << e.what();
        return false;
    }
}

