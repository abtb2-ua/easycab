# Kafka
docker run -d --rm --name=kafka-server -p 9092:9092 apache/kafka:3.8.0
docker exec -ti kafka-server /bin/bash 
cd opt/kafka

bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic map

# MySql server
docker run --rm --name mysql-server -e MYSQL_ROOT_PASSWORD=1234 -v ./src/initialize.sql:/docker-entrypoint-initdb.d/script.sql -p 3306:3306 -d mysql:latest

mysql -h 127.0.0.1 -P 3306 -u root -p


# Apps
export G_MESSAGES_DEBUG=all
(unset G_MESSAGES_DEBUG)

./build/EC_Central 8081 localhost:9092 127.0.0.1:3306

# Restart topics

bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic map &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic mov &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --delete --topic petitions &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic map &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic mov &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic responses &&
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic petitions




