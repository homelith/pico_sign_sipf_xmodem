#!/bin/bash
/usr/sbin/nginx -g "daemon off;" &
gunicorn --bind 0.0.0.0:8000 -w 2 pico_sign_web.wsgi:application
