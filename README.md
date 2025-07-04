# Установка зависимостей
    sudo apt-get update
    sudo apt-get install -y cmake libssl-dev
    sudo apt-get install -y libpqxx-dev
    
    sudo apt-get install -y postgresql postgresql-contrib
    sudo systemctl start postgresql
    sudo systemctl enable postgresql

# Сборка и установка AMQP-CPP
    git clone https://github.com/CopernicaMarketingSoftware/AMQP-CPP.git
    cd AMQP-CPP
    mkdir build
    cd build
    cmake .. -DAMQP-CPP_BUILD_SHARED=ON -DAMQP-CPP_LINUX_TCP=ON
    make
    sudo make install
    sudo ldconfig

# Зависимости

Qt5 (Core, Network)

libpqxx (для работы с PostgreSQL)

PHP-CGI (для выполнения PHP скриптов)

# Как использовать

API для работы с БД:

    GET /api/db/table_name - получить все записи из таблицы /api/db/users
    echo -n "admin:admin123" | base64
    GET /api/db/table_name?param1=value1&param2=value2 - получить отфильтрованные записи http://localhost:8080/api/db/users?username=admin

    POST /api/db/table_name - добавить новую запись (параметры в теле запроса)


PHP скрипты:

    Разместите .php файлы в директории www, они будут выполняться через php-cgi по запросу

Статические файлы:

    HTML, CSS, JS и другие файлы будут обслуживаться как статические

Особенности реализации

    PHP скрипты выполняются через CGI интерфейс

    Для работы с PostgreSQL используется libpqxx

    Конфигурация сервера хранится в INI-файле
