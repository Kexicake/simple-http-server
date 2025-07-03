document.addEventListener('DOMContentLoaded', () => {
    const baseUrl = 'http://localhost:8080/api';

    // Элементы DOM
    const getUsersBtn = document.getElementById('getUsers');
    const usersResult = document.getElementById('usersResult');
    const sendMessageBtn = document.getElementById('sendMessage');
    const getMessagesBtn = document.getElementById('getMessages');
    const rabbitResult = document.getElementById('rabbitResult');

    // Авторизация (Basic Auth)
    const headers = new Headers();
    headers.set('Authorization', 'Basic ' + btoa('admin:admin123'));

    // Получение пользователей
    getUsersBtn.addEventListener('click', async () => {
        try {
            const response = await fetch(`${baseUrl}/db/users`, { headers });
            const data = await response.json();
            usersResult.innerHTML = `<pre>${JSON.stringify(data, null, 2)}</pre>`;
        } catch (error) {
            usersResult.innerHTML = `Ошибка: ${error.message}`;
        }
    });
});
