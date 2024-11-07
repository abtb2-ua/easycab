# Kafka
docker run --rm --name=kafka-server -p 9092:9092 apache/kafka:3.8.0
docker exec -ti kafka-server /bin/bash 
cd opt/kafka

bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic map

# MySql server
docker run --rm --name mysql-server -e MYSQL_ROOT_PASSWORD=1234 -v ./src/initialize.sql:/docker-entrypoint-initdb.d/script.sql -p 3306:3306 mysql:latest

mysql -h 127.0.0.1 -P 3306 -u root -p


# Apps
export G_MESSAGES_DEBUG=all
(unset G_MESSAGES_DEBUG)

./build/EC_Central 8081 localhost:9092 127.0.0.1:3306

# Restart topics

bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic customer_responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic taxi_responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic map_responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic requests &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic customer_responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic taxi_responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic map_responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic requests

cmake --build build && ./build/gui
cmake --build build && ./build/EC_Central 2400 localhost:9092 127.0.0.1:3306 
cmake --build build && ./build/EC_DE 192.168.0.17:2400 localhost:9092 8000 5
cmake --build build && ./build/EC_Customer localhost:9092 b 11 5

sudo docker run --rm -e TERM=xterm-256color -ti easycab_image