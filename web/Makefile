.PHONY: default pipenv initdb run docker

default : run

pipenv :
	DISPLAY= PIPENV_VENV_IN_PROJECT=1 pipenv install

initdb :
	DISPLAY= pipenv run python3 gen_secret_key.py > pico_sign_web/settings/secret_key.py
	DISPLAY= pipenv run python3 manage.py migrate --settings=pico_sign_web.settings.settings_local
	DISPLAY= pipenv run python3 manage.py createsuperuser --settings=pico_sign_web.settings.settings_local

run :
	DISPLAY= pipenv run python3 manage.py runserver 0.0.0.0:8080 --settings=pico_sign_web.settings.settings_local

docker :
	docker build -t pico_sign_web:latest .


