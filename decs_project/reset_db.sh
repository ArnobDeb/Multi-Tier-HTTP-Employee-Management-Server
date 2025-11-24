#!/usr/bin/env bash
# reset_db.sh — Truncate employees table (and optionally re-create it)

USER="empuser"
PASS="emppass"
DB="company"

echo "Truncating employees table..."
mysql -u "$USER" -p"$PASS" -e "TRUNCATE TABLE ${DB}.employees;"
if [ $? -eq 0 ]; then
  echo "DB reset OK."
else
  echo "DB reset failed — try running 'mysql -u root -p < setup_company.sql' to recreate."
fi
