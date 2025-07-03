-- sudo -u postgres psql -f init_db.sql
-- sudo chmod o+rx /home
-- Создаем базу данных (если не существует)
CREATE DATABASE simple_http_db;

-- Подключаемся к базе
\c simple_http_db

-- Таблица пользователей
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password VARCHAR(100) NOT NULL,
    email VARCHAR(100) UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Таблица сообщений для RabbitMQ
CREATE TABLE rabbit_messages (
    id SERIAL PRIMARY KEY,
    queue_name VARCHAR(100) NOT NULL,
    message TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Тестовые данные
INSERT INTO users (username, password, email) VALUES
('admin', 'admin123', 'admin@example.com'),
('user1', 'password1', 'user1@example.com');

INSERT INTO rabbit_messages (queue_name, message) VALUES
('test_queue', 'Первое тестовое сообщение'),
('test_queue', 'Второе тестовое сообщение');
