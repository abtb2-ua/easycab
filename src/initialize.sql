DROP DATABASE IF EXISTS db;
CREATE DATABASE db;
use db;

CREATE TABLE session (
  id CHAR(40) PRIMARY KEY
);

CREATE TABLE locations (
  id CHAR PRIMARY KEY,
  x INT NOT NULL,
  y INT NOT NULL
);

CREATE TABLE customers (
  id CHAR NOT NULL PRIMARY KEY,
  destination CHAR, 
  last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  x INT NOT NULL,
  y INT NOT NULL,
  in_queue TIMESTAMP DEFAULT NULL,
  CONSTRAINT customer_destination_fk FOREIGN KEY (destination) REFERENCES locations (id)
);

CREATE TABLE taxis (
  id INT NOT NULL PRIMARY KEY, 
  -- Whether its applications hasn't got stuck or disconnected abruptly
  connected BOOLEAN NOT NULL DEFAULT TRUE,
  last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  -- Whether is doing a service
  available BOOL NOT NULL DEFAULT TRUE,
  -- Whether it's supposed to be moving
  moving BOOL NOT NULL DEFAULT FALSE,
  -- Whether taxi can move (e.g. if sensor disconnected or sent error to taxi)
  can_move BOOL NOT NULL DEFAULT FALSE,
  -- Whether it's carrying a customer
  carrying_customer BOOL NOT NULL DEFAULT FALSE, 
  -- Who's the taxi carrying or moving towards
  customer CHAR DEFAULT NULL,
  x INT NOT NULL DEFAULT 0,
  y INT NOT NULL DEFAULT 0,
  CONSTRAINT taxis_customer_fk FOREIGN KEY (customer) REFERENCES customers (id),
  CONSTRAINT taxis_id CHECK (id >= 0 AND id <= 99)
);

DELIMITER !! 

CREATE PROCEDURE ResetDB()
BEGIN
  DELETE FROM taxis;
  DELETE FROM customers;
  DELETE FROM locations;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE LoadMap()
BEGIN
  SELECT id, x, y FROM locations;
  
  SELECT id, x, y, destination, in_queue IS NOT NULL, 
  EXISTS(SELECT 1 FROM taxis WHERE customer = c.id AND carrying_customer = TRUE) 
  FROM customers c;

  SELECT id, x, y, customer, moving, carrying_customer, connected, can_move FROM taxis;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE ConnectTaxi(
  IN id INT
)
begin_label: BEGIN
  IF EXISTS (SELECT 1 FROM taxis t WHERE t.id = id) THEN
    IF EXISTS (SELECT 1 FROM taxis t WHERE t.id = id AND t.connected = TRUE) THEN
      SELECT 'Taxi already connected';
      LEAVE begin_label;
    END IF;
    UPDATE taxis t SET t.connected = TRUE WHERE t.id = id;
    SELECT 0;
  ELSE 
    INSERT INTO taxis (id) VALUES (id);
    SELECT 1;
  END IF;
END !!

-- ------------------------------------------------------------------------------

-- From here on, the procedures will output in form of selects. 
-- The first select will always be reserved for errors or NULL if there aren't any.

CREATE PROCEDURE InsertCustomer(
  IN id CHAR,
  IN x INT,
  IN y INT
)
begin_label: BEGIN
  IF EXISTS (SELECT 1 FROM customers c WHERE c.id = id) THEN
    SELECT 'customer already exists'; 
    LEAVE begin_label;
  END IF;

  INSERT INTO customers (id, x, y) VALUES (id, x, y);

  SELECT NULL;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE AssignTaxi(
  IN customerId CHAR, 
  IN destination CHAR
)
begin_label: BEGIN
  DECLARE customer_x INT;
  DECLARE customer_y INT;
  DECLARE taxiId INT;

  SELECT x, y INTO customer_x, customer_y FROM customers c WHERE c.id = customerId;
  IF customer_x IS NULL OR customer_y IS NULL THEN
    SELECT 'customer not found';
    LEAVE begin_label;
  END IF;

   
  IF NOT EXISTS (SELECT 1 FROM locations l WHERE l.id = destination) THEN
    SELECT 'Destination not found';
    LEAVE begin_label;
  END IF;

  UPDATE customers c SET c.destination = destination WHERE c.id = customerId;

  SELECT MIN(t.id) INTO taxiId FROM taxis t WHERE t.connected = TRUE AND t.available = TRUE;
  IF taxiId IS NULL THEN
    SELECT NULL; -- First to indicate there have been no errors
    SELECT NULL; -- Second to indicate there aren't any available taxis
    LEAVE begin_label;
  END IF;

  UPDATE taxis t SET t.available = FALSE, t.moving = TRUE, t.customer = customerId WHERE t.id = taxiId; 

  SELECT NULL;

  SELECT customer_x, customer_y, taxiId;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE AddToQueue(
  IN customerId CHAR
)
begin_label: BEGIN
  IF NOT EXISTS (SELECT 1 FROM customers c WHERE c.id = customerId) THEN
    SELECT 'customer not found';
    LEAVE begin_label;
  END IF;

  UPDATE customers c SET c.in_queue = CURRENT_TIMESTAMP WHERE c.id = customerId;

  SELECT NULL;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE GetCustomerFromQueue()
begin_label: BEGIN
  DECLARE customerId CHAR;
  
  IF NOT EXISTS (SELECT 1 FROM customers c WHERE c.in_queue IS NOT NULL) THEN
    SELECT NULL;
    LEAVE begin_label;
  END IF;

  SELECT c.id INTO customerId FROM customers c 
  WHERE c.in_queue IS NOT NULL 
  ORDER BY in_queue LIMIT 1;

  UPDATE customers c SET c.in_queue = NULL WHERE c.id = customerId;

  SELECT c.id, c.destination FROM customers c WHERE c.id = customerId;
END !!

-- ------------------------------------------------------------------------------ 

CREATE PROCEDURE GetTaxiStatus(
  IN taxiId INT
)
begin_label: BEGIN
  DECLARE taxi_x INT;
  DECLARE taxi_y INT;
  DECLARE carrying_customer BOOL;
  DECLARE customer CHAR;
  DECLARE destination_x INT;
  DECLARE destination_y INT;

  SELECT t.x, t.y, t.carrying_customer, t.customer
  INTO taxi_x, taxi_y, carrying_customer, customer 
  FROM taxis t WHERE id = taxiId;

  IF taxi_x IS NULL OR taxi_y IS NULL THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  SELECT NULL;
  
  IF customer IS NULL THEN 
    SELECT 0, taxi_x, taxi_y;
    UPDATE taxis SET moving = FALSE WHERE id = taxiId;
  ELSEIF NOT carrying_customer THEN 
    SELECT 1;
  ELSE
    SELECT l.x, l.y INTO destination_x, destination_y 
    FROM customers c LEFT JOIN locations l ON l.id = c.destination
    WHERE c.id = customer; 
    IF taxi_x = destination_x AND taxi_y = destination_y THEN
      SELECT 2;
    ELSE 
      IF carrying_customer THEN 
        SELECT 3, destination_x, destination_y;
      ELSE 
        SELECT 3, c.x, c.y FROM customers c WHERE c.id = customer;
      END IF;
    END IF;
  END IF;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE PickUpCustomer(
  IN taxiId INT
)
begin_label: BEGIN
  DECLARE customerId CHAR;
  DECLARE destination CHAR;
  DECLARE destination_x INT;
  DECLARE destination_y INT;

  IF NOT EXISTS (SELECT 1 FROM taxis t WHERE t.id = taxiId) THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  SELECT c.id, c.destination, l.x, l.y
  INTO customerId, destination, destination_x, destination_y
  FROM taxis t 
  LEFT JOIN customers c ON t.customer = c.id
  LEFT JOIN locations l ON l.id = c.destination
  WHERE t.id = taxiId;

  UPDATE taxis SET carrying_customer = TRUE WHERE id = taxiId;

  SELECT NULL;

  SELECT customerId, destination, destination_x, destination_y;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE CompleteService(
  IN taxiId INT
)
begin_label: BEGIN
  DECLARE customerId CHAR;
  DECLARE destination CHAR;
  DECLARE destination_x INT;
  DECLARE destination_y INT;

  IF NOT EXISTS (SELECT 1 FROM taxis t WHERE t.id = taxiId) THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  SELECT t.customer, t.x, t.y, c.destination INTO customerId, destination_x, destination_y, destination
  FROM taxis t LEFT JOIN customers c ON t.customer = c.id
  WHERE t.id = taxiId;

  UPDATE customers c SET c.destination = NULL, c.x = destination_x, c.y = destination_y
  WHERE c.id = customerId;

  UPDATE taxis t SET t.available = TRUE, t.moving = FALSE, t.customer = NULL, t.carrying_customer = FALSE
  WHERE t.id = taxiId;

  SELECT NULL;

  SELECT customerId, destination, destination_x, destination_y;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE MoveTaxi (
  IN taxiId INT,
  IN x INT,
  IN y INT
)
begin_label: BEGIN
  DECLARE connected BOOL;
  DECLARE moving BOOL;
  DECLARE current_x INT;
  DECLARE current_y INT;

  SELECT t.connected, t.moving, t.x, t.y
  INTO connected, moving, current_x, current_y 
  FROM taxis t WHERE id = taxiId;

  IF connected IS NULL OR moving IS NULL THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  IF connected IS FALSE THEN
    SELECT CONCAT('Taxi ', taxiId, ' tried to move but it is considered as disconnected');
    LEAVE begin_label;
  END IF;

  IF moving IS FALSE THEN
    SELECT CONCAT('Taxi ', taxiId, ' tried to move but it is supposed to be stopped');
    LEAVE begin_label;
  END IF;

  UPDATE taxis t SET t.x = x, t.y = y WHERE t.id = taxiId;
  SELECT NULL;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE ChangeMotion(
  IN taxiId INT,
  IN moving BOOL
)
begin_label: BEGIN
  IF NOT EXISTS (SELECT 1 FROM taxis t WHERE t.id = taxiId) THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  UPDATE taxis t SET t.moving = moving WHERE t.id = taxiId;
  SELECT NULL;
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE DisconnectTaxi(
  IN taxiId INT
)
begin_label: BEGIN
  DECLARE customerId CHAR;
  DECLARE carrying_customer BOOL;
  DECLARE coord_x INT;
  DECLARE coord_y INT;

  IF NOT EXISTS (SELECT 1 FROM taxis t WHERE t.id = taxiId) THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  SELECT NULL;

  SELECT t.customer, t.x, t.y, t.carrying_customer 
  INTO customerId, coord_x, coord_y, carrying_customer
  FROM taxis t WHERE t.id = taxiId;

  IF customerId IS NOT NULL THEN
    -- Set the minimum value of timestamp to get the maximum priority in the queue
    UPDATE customers c SET in_queue = FROM_UNIXTIME(1) WHERE c.id = customerId;
    
    IF carrying_customer THEN
      SELECT customerId, coord_x, coord_y;
      UPDATE customers c SET c.x = coord_x, c.y = coord_y WHERE c.id = customerId;
    ELSE 
      SELECT NULL;
    END IF;
  ELSE
    SELECT NULL;
  END IF;

  -- Reset to default values besides disconnecting
  DELETE FROM taxis t WHERE t.id = taxiId;
  INSERT INTO taxis(id, connected, x, y) VALUES (taxiId, FALSE, coord_x, coord_y);
END !!

-- ------------------------------------------------------------------------------

CREATE PROCEDURE CheckStrays(
  IN grace_time INT
)
BEGIN
  SELECT TRUE, t.id FROM taxis t WHERE t.connected = TRUE AND t.last_update < NOW() - INTERVAL grace_time SECOND;
  SELECT FALSE, c.id FROM customers c
  WHERE NOT EXISTS (SELECT 1 FROM taxis t WHERE t.customer = c.id)
  AND c.last_update < NOW() - INTERVAL grace_time SECOND;
END !!

DELIMITER ;