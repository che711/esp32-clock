### Simple api data

# Получить время
curl http://192.168.1.33/api/time

# Красиво отформатировать
curl -s http://192.168.1.33/api/time | python3 -m json.tool

# Только время в строку
curl -s http://192.168.1.33/api/time | grep -o '"time":"[^"]*"'

# Полная статистика
curl http://192.168.1.33/api/stats
