#!/usr/bin/env bash
# =============================================================
#  Interactive Client for Employee HTTP Server
#  Usage:
#     ./client.sh [HOST] [PORT]
#  Example:
#     ./client.sh 127.0.0.1 8080
#
#  Then enter commands interactively:
#     > POST 1 Alice HR 50000
#     > GET 1
#     > PUT 1 Finance 70000
#     > DELETE 1
#     > exit
# =============================================================

HOST=${1:-127.0.0.1}
PORT=${2:-8080}
URL="http://$HOST:$PORT/employee"

GREEN="\033[1;32m"
RED="\033[1;31m"
YELLOW="\033[1;33m"
RESET="\033[0m"

banner() {
    echo -e "${GREEN}"
    echo "=========================================================="
    echo "   Employee Management Client - CS744 Phase 1"
    echo "=========================================================="
    echo -e "${RESET}"
    echo "Connected to: $URL"
    echo "Type commands like:"
    echo "  POST <id> <name> <department> <salary>"
    echo "  GET <id>"
    echo "  LIST"
    echo "  PUT <id> <department> <salary>"
    echo "  DELETE <id>"
    echo "  exit"
    echo ""
}

banner

while true; do
    echo -ne "${YELLOW}> ${RESET}"
    read -r line || break
    [ -z "$line" ] && continue

    args=($line)
    method=$(echo "${args[0]}" | tr '[:lower:]' '[:upper:]')

    case "$method" in
        POST)
            if [ ${#args[@]} -ne 5 ]; then
                echo -e "${RED}Usage:${RESET} POST <id> <name> <department> <salary>"
                continue
            fi
            id=${args[1]}
            name=${args[2]}
            dept=${args[3]}
            salary=${args[4]}
            data="id=${id}&name=${name}&department=${dept}&salary=${salary}"
            echo -e "${GREEN}→ Creating employee:${RESET} ID=$id, Name=$name, Dept=$dept, Salary=$salary"
            curl -s -X POST -d "$data" "$URL"
            echo -e "\n"
            ;;

        GET)
            if [ ${#args[@]} -ne 2 ]; then
                echo -e "${RED}Usage:${RESET} GET <id>"
                continue
            fi
            id=${args[1]}
            echo -e "${GREEN}→ Fetching employee ID:${RESET} $id"
            curl -s "$URL/$id"
            echo -e "\n"
            ;;

        LIST)
            echo -e "${GREEN}→ Listing all employees...${RESET}"
            curl -s "$URL/all"
            echo -e "\n"
            ;;

        PUT)
            if [ ${#args[@]} -ne 4 ]; then
                echo -e "${RED}Usage:${RESET} PUT <id> <department> <salary>"
                continue
            fi
            id=${args[1]}
            dept=${args[2]}
            salary=${args[3]}
            data="department=${dept}&salary=${salary}"
            echo -e "${GREEN}→ Updating employee:${RESET} ID=$id, Dept=$dept, Salary=$salary"
            curl -s -X PUT -d "$data" "$URL/$id"
            echo -e "\n"
            ;;

        DELETE)
            if [ ${#args[@]} -ne 2 ]; then
                echo -e "${RED}Usage:${RESET} DELETE <id>"
                continue
            fi
            id=${args[1]}
            echo -e "${GREEN}→ Deleting employee ID:${RESET} $id"
            curl -s -X DELETE "$URL/$id"
            echo -e "\n"
            ;;

        EXIT|QUIT)
            echo -e "${YELLOW}Exiting client...${RESET}"
            break
            ;;

        *)
            echo -e "${RED}Unknown command:${RESET} $method"
            echo "Try: POST, GET, PUT, DELETE, LIST, exit"
            ;;
    esac
done
