<?php
header("Content-Type: application/json");

// Простейший роутинг
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    try {
        $db = new PDO('pgsql:host=localhost;dbname=simple_http_db', 'postgres', 'postgres');
        $stmt = $db->query('SELECT * FROM users');
        $users = $stmt->fetchAll(PDO::FETCH_ASSOC);

        echo json_encode([
            'status' => 'success',
            'data' => $users
        ]);
    } catch (PDOException $e) {
        http_response_code(500);
        echo json_encode([
            'status' => 'error',
            'message' => 'Database error: ' . $e->getMessage()
        ]);
    }
} else {
    http_response_code(405);
    echo json_encode([
        'status' => 'error',
        'message' => 'Method not allowed'
    ]);
}
?>
