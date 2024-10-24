DROP DATABASE IF EXISTS db;
CREATE DATABASE db;
use db;

CREATE TABLE locations (
  id CHAR PRIMARY KEY,
  x INT NOT NULL,
  y INT NOT NULL
);

CREATE TABLE clients (
  id CHAR NOT NULL PRIMARY KEY,
  destination CHAR, 
  x INT NOT NULL,
  y INT NOT NULL,
  CONSTRAINT client_destination_fk FOREIGN KEY (destination) REFERENCES locations (id)
);

CREATE TABLE taxis (
  id INT NOT NULL PRIMARY KEY, 
  -- Whether its applications hasn't got stuck or disconnected abruptly
  connected BOOLEAN NOT NULL DEFAULT TRUE,
  last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  -- Whether is doing a service
  available BOOL NOT NULL DEFAULT TRUE,
  -- Whether it's moving 
  moving BOOL NOT NULL DEFAULT FALSE,
  -- Whether it's carrying a client
  carrying_client BOOL NOT NULL DEFAULT FALSE, 
  -- Who's the taxi carrying or moving towards
  client CHAR DEFAULT NULL,
  x INT NOT NULL DEFAULT 0,
  y INT NOT NULL DEFAULT 0,
  CONSTRAINT taxis_client_fk FOREIGN KEY (client) REFERENCES clients (id),
  CONSTRAINT taxis_id CHECK (id >= 0 AND id <= 99)
);

DELIMITER !! 

CREATE PROCEDURE ResetDB()
BEGIN
  DELETE FROM taxis;
  DELETE FROM clients;
  DELETE FROM locations;
END !!

--------------------------------------------------------------------------------

CREATE PROCEDURE LoadMap()
BEGIN
  -- Locations
  SELECT id, x, y FROM locations;
  -- Clients that are not in any taxi
  SELECT id, x, y FROM clients c 
  WHERE NOT EXISTS 
  (SELECT 1 FROM taxis t 
   WHERE t.client = c.id 
   AND t.carrying_client = TRUE);
  -- Taxis
  SELECT id, x, y, moving, client, carrying_client FROM taxis;
END !!

--------------------------------------------------------------------------------

CREATE FUNCTION GetNextUnusedId() 
RETURNS INT 
NOT DETERMINISTIC
READS SQL DATA

BEGIN
  DECLARE nextUnusedId INT DEFAULT NULL;

  SELECT id INTO nextUnusedId FROM taxis WHERE id = 0;

  IF nextUnusedId IS NULL THEN 
    RETURN 0;
  END IF;

  SELECT MIN(id + 1) INTO nextUnusedId 
  FROM taxis WHERE id + 1 
  NOT IN (SELECT id FROM taxis) 
  AND (id + 1) < 100;
  
  RETURN nextUnusedId;
END !!

--------------------------------------------------------------------------------

-- From here on, the procedures will output in form of selects. 
-- The first select will always be reserved for errors or NULL if there aren't any.

CREATE PROCEDURE InsertClient(
  IN id CHAR(1),
  IN x INT,
  IN y INT
)
begin_label: BEGIN
  IF EXISTS (SELECT 1 FROM clients c WHERE c.id = id) THEN
    SELECT 'Client already exists'; 
    LEAVE begin_label;
  END IF;

  INSERT INTO clients (id, x, y) VALUES (id, x, y);

  SELECT NULL;
END !!

--------------------------------------------------------------------------------

CREATE PROCEDURE AssignTaxi(
  IN clientId CHAR(1), 
  IN destination CHAR(1)
)
begin_label: BEGIN
  DECLARE client_x INT;
  DECLARE client_y INT;
  DECLARE dest_x INT;
  DECLARE dest_y INT;
  DECLARE taxiId INT;

  SELECT x, y INTO client_x, client_y FROM clients c WHERE c.id = clientId;
  IF client_x IS NULL OR client_y IS NULL THEN
    SELECT 'Client not found';
    LEAVE begin_label;
  END IF;

  SELECT l.x, l.y INTO dest_x, dest_y FROM locations l WHERE l.id = destination;
  IF dest_x IS NULL OR dest_y IS NULL THEN
    SELECT 'Destination not found';
    LEAVE begin_label;
  END IF;

  SELECT MIN(t.id) INTO taxiId FROM taxis t WHERE t.connected = TRUE AND t.available = TRUE;
  IF taxiId IS NULL THEN
    SELECT 'No available taxi found';
    LEAVE begin_label;
  END IF;

  UPDATE clients c SET c.destination = destination WHERE c.id = clientId;
  UPDATE taxis t SET t.available = FALSE, t.moving = TRUE, t.client = clientId WHERE t.id = taxiId; 

  SELECT NULL;

  SELECT client_x, client_y, dest_x, dest_y, taxiId;
END !!

--------------------------------------------------------------------------------

CREATE PROCEDURE PickUpClient(
  IN taxiId CHAR(1)
)
begin_label: BEGIN
  DECLARE clientId CHAR(1);
  DECLARE destination CHAR(1);

  IF NOT EXISTS (SELECT 1 FROM taxis t WHERE t.id = taxiId) THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  SELECT c.id, c.destination INTO clientId, destination 
  FROM taxis t LEFT JOIN clients c ON t.client = c.id
  WHERE t.id = taxiId;

  UPDATE taxis SET carrying_client = TRUE WHERE id = taxiId;

  SELECT NULL;

  SELECT clientId, destination;
END !!

--------------------------------------------------------------------------------

CREATE PROCEDURE CompleteService(
  IN taxiId CHAR(1)
)
begin_label: BEGIN
  DECLARE clientId CHAR(1);
  DECLARE destination CHAR(1);
  DECLARE destination_x INT;
  DECLARE destination_y INT;

  IF NOT EXISTS (SELECT 1 FROM taxis t WHERE t.id = taxiId) THEN
    SELECT 'Taxi not found';
    LEAVE begin_label;
  END IF;

  SELECT t.client, t.x, t.y, c.destination INTO clientId, destination_x, destination_y, destination
  FROM taxis t LEFT JOIN clients c ON t.client = c.id
  WHERE t.id = taxiId;

  UPDATE clients c SET c.destination = NULL, c.x = destination_x, c.y = destination_y
  WHERE c.id = clientId;

  UPDATE taxis t SET t.available = TRUE, t.moving = FALSE, t.client = NULL, t.carrying_client = FALSE
  WHERE t.id = taxiId;

  SELECT NULL;

  SELECT clientId, destination, destination_x, destination_y;
END !!

--------------------------------------------------------------------------------


DELIMITER ;