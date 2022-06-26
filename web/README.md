# web interface for pico\_sign\_sign\_xmodem

- SQLite3 (db.sqlite3 に作成される) を DB にした動作
```
PIPENV_VENV_IN_PROJECT=1 pipenv install
pipenv run python3 manage.py migrate
pipenv run python3 gen_secret_key.py > pico_sign_web/secret_key.py
pipenv run python3 manage.py createsuperuser
pipenv run python3 manage.py runserver
```

- runserver した後 `http://localhost:8080/` でアクセス

