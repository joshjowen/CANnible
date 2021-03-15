#!/bin/bash
cd `dirname "${BASH_SOURCE[0]}"`
SERVE_PORT="8000"
build/CANnible &
cd html
sleep 2

echo Serving:
find . -type f -name "*.html" -printf "\thttp://`hostname -I | awk '{print $1}'`:$SERVE_PORT/%f\n"

python3 -m http.server $SERVE_PORT 2>&1>/dev/null