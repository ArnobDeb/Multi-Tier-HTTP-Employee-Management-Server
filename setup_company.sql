-- ==========================================================
--  CS744 DECS Project - Phase 1
--  Database Initialization Script: setup_company.sql
--  Author: Arnob Deb
--  Purpose: Create the 'company' database and 'employees' table
--           with manual (user-assigned) employee IDs.
-- ==========================================================

-- Drop existing database if you want a clean setup (optional)
DROP DATABASE IF EXISTS company;

-- Create new database
CREATE DATABASE company;

-- Select it for use
USE company;

-- Create 'employees' table with user-assigned IDs (no AUTO_INCREMENT)
CREATE TABLE employees (
    id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    department VARCHAR(100) NOT NULL,
    salary DOUBLE NOT NULL
);

-- Create a dedicated MySQL user for the project (optional, if not already exists)
CREATE USER IF NOT EXISTS 'empuser'@'localhost' IDENTIFIED BY 'emppass';

-- Grant privileges on this database
GRANT ALL PRIVILEGES ON company.* TO 'empuser'@'localhost';
FLUSH PRIVILEGES;

-- Optional sample data for quick verification
INSERT INTO employees (id, name, department, salary) VALUES
(1, 'Alice', 'HR', 50000),
(2, 'Bob', 'Finance', 60000),
(3, 'Clara', 'IT', 70000);

-- Show final contents (for confirmation if run manually)
SELECT * FROM employees;
