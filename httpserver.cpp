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

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent),
    m_settings(new QSettings("http_server.ini", QSettings::IniFormat))
{
    // Загрузка конфигурации
    setDocumentRoot(m_settings->value("server/document_root", QCoreApplication::applicationDirPath() + "/www").toString());
    setPhpCgiPath(m_settings->value("php/cgi_path", "/usr/bin/php-cgi").toString());

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
    qInfo() << "Document root:" << m_documentRoot;
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
    m_phpCgiPath = path;
    m_settings->setValue("php/cgi_path", m_phpCgiPath);
}

void HttpServer::setPhpCgiPath(const QString &path)
{
    m_phpCgiPath = "/usr/bin/php-cgi";
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
        for (int i = 0; i < lines.size(); ++i)
            qInfo() << lines[i];
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
        if (i + 1 < lines.size()) {
            body = lines.mid(i + 1).join("\r\n").toUtf8();
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
        QMap<QString, QString> params = parseQueryParams(url.query());
        return serveApi(apiPath, params, body);
    }

    // Определение файла для обслуживания
    QString filePath = m_documentRoot + cleanPath;
    if (filePath.endsWith('/')) {
        filePath += "index.html";
    }

    QFileInfo fileInfo(filePath);

    // Проверка существования файла
    if (!fileInfo.exists()) {
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


QByteArray HttpServer::serveApi(const QString &apiPath, const QMap<QString, QString> &params, const QByteArray &postData)
{
    // Обработка API запросов к базе данных
    if (apiPath.startsWith("db/")) {
        QString table = apiPath.mid(3); // Убираем "db/"
        return handleDbRequest(table, params, postData.isEmpty() ? "GET" : "POST");
    }

    return createErrorResponse(404, "API endpoint not found");
}

QByteArray HttpServer::handleDbRequest(const QString &table, const QMap<QString, QString> &params, const QString &method)
{
    if (!m_dbConnection) {
        return createErrorResponse(500, "Database not connected");
    }

    try {
        pqxx::work txn(*m_dbConnection);
        QJsonObject result;

        if (method == "GET") {
            // Запрос данных из таблицы
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

            pqxx::result res = txn.exec(query);

            QJsonArray rows;
            for (auto row : res) {
                QJsonObject obj;
                for (auto field : row) {
                    obj[QString::fromStdString(field.name())] = QString::fromStdString(field.c_str());
                }
                rows.append(obj);
            }

            result["status"] = "success";
            result["data"] = rows;
        }
        else if (method == "POST") {
            // Вставка данных в таблицу
            if (params.isEmpty()) {
                return createErrorResponse(400, "No data provided");
            }

            std::string columns, values;
            bool first = true;
            for (auto it = params.begin(); it != params.end(); ++it) {
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
        }

        return createJsonResponse(result);
    }
    catch (const std::exception &e) {
        return createErrorResponse(500, QString("Database error: ") + e.what());
    }
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
        return false;
    }

    QByteArray authData = QByteArray::fromBase64(authHeader.mid(6).toUtf8());
    QString authStr = QString::fromUtf8(authData);
    QStringList parts = authStr.split(':');
    if (parts.size() != 2) {
        return false;
    }

    QString username = parts[0];
    QString password = parts[1];

    // Проверка учетных данных
    QString storedUser = m_settings->value("auth/username", "admin").toString();
    QString storedPass = m_settings->value("auth/password", "admin").toString();

    return (username == storedUser && password == storedPass);
}

