<?php
// Пример API endpoint на PHP
header("Content-Type: application/json");

$response = [
    'status' => 'success',
    'message' => 'Это тестовый ответ от PHP скрипта',
    'data' => [
        'timestamp' => time(),
        'random_number' => rand(1, 100)
    ]
];

echo json_encode($response);
?>
