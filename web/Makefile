.PHONY: default init run

default : run

init :
	DISPLAY= PIPENV_VENV_IN_PROJECT=1 pipenv install
	DISPLAY= pipenv run python3 gen_secret_key.py > signboard/secret_key.py
	DISPLAY= pipenv run python3 manage.py migrate
	DISPLAY= pipenv run python3 manage.py createsuperuser

run :
	DISPLAY= pipenv run python3 manage.py runserver 8080
