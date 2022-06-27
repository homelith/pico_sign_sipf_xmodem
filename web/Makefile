.PHONY: default pipenv initdb run docker

default : run

pipenv :
	DISPLAY= PIPENV_VENV_IN_PROJECT=1 pipenv install

initdb :
	DISPLAY= pipenv run python3 gen_settings_extra.py > pico_sign_web/settings_extra.py
	DISPLAY= pipenv run python3 manage.py migrate
	DISPLAY= pipenv run python3 manage.py createsuperuser

run :
	DISPLAY= pipenv run python3 manage.py runserver 0.0.0.0:8080

docker :
	docker build -t pico_sign_web:latest .


