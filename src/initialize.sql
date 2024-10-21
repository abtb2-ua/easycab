create database db;
use db;

create table locations (
  id char primary key,
  x int not null,
  y int not null
);

create table clients (
  id char not null primary key,
  destination char, 
  x int not null,
  y int not null,
  constraint client_destination_fk foreign key (destination) references locations (id)
);

create table taxis (
  id int not null primary key, 
  -- Whether its applications hasn't got stucked or disconnected abruptly
  connected boolean not null default true,
  last_update timestamp not null default current_timestamp on update current_timestamp,
  -- Whether is doing a service
  available bool not null default true,
  -- Whether it's moving 
  moving bool not null default false,
  -- Whether it's carrying a client
  carrying_client bool not null default false, 
  -- Who's the taxi carrying or moving towards
  client char default null,
  x int not null default 0,
  y int not null default 0,
  constraint taxis_client_fk foreign key (client) references clients (id),
  constraint taxis_id check (id >= 0 and id <= 99)
);

DELIMITER // 
CREATE PROCEDURE ResetDB()
BEGIN
  DELETE FROM taxis;
  DELETE FROM clients;
  DELETE FROM locations;
END //
DELIMITER ;