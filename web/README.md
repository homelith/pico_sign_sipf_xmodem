# web interface for pico\_sign\_sign\_xmodem

## prerequesites

- ubuntu 22.04 LTS
- install pkgs

```
sudo apt install python3-pip
pip install pipenv
```

## testing on local environment

- cloning

```
git clone git@github.com:homelith/pico_sign_sipf_xmodem.git
cd pico_sign_sipf_xmodem/web
```

- set mono platform credentials to `pico_sign_web/settings/monopla.py`

```
SAKURACLOUD_ACCESS_TOKEN = ''
SAKURACLOUD_ACCESS_TOKEN_SECRET = ''
SAKURACLOUD_COMMONSERVICEITEM_ENDPOINT = ''
SIPF_WEBHOOK_ENDPOINT = ''
SIPF_PROJECT_ID = ''
SIPF_DEVICE_ID = ''
```

- `make pipenv` to run pipenv install

```
PIPENV_VENV_IN_PROJECT=1 pipenv install
```

- `make initdb` to run initialize database

```
pipenv run python3 gen_secret_key.py > pico_sign_web/settings/secret_key.py
pipenv run python3 manage.py migrate --settings=pico_sign_web.settings.settings_local
pipenv run python3 manage.py createsuperuser --settings=pico_sign_web.settings.settings_local
```

- `make run` to run django development server on your localhost:808

```
pipenv run python3 manage.py runserver 0.0.0.0:8080 --settings=pico_sign_web.settings.settings_local
```

## deploying on sakura cloud

- set up enhanced DB (managed MySQL compatible distributed database)

```

```

- set up virtual server with ubuntu 22.04 LTS image

```
```

- install docker

```
sudo apt install apt-transport-https ca-certificates curl software-properties-common
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt update
sudo apt install docker-ce
```

- clone and set parameters

```
```

- `make docker` to build docker image

```
docker build -t pico_sign_web:latest .
```

- register `/etc/systemd/system/pico_sign_web.service` running on boot

```
[Unit]
Description=PicoSignWeb
Requires=docker.service
After=docker.service

[Service]
WorkingDirectory=/opt
Type=simple

ExecStartPre=-/usr/bin/docker stop pico_sign_web
ExecStartPre=-/usr/bin/docker rm pico_sign_web
#ExecStartPre=/usr/bin/docker pull pico_sign_web:latest
ExecStart=/usr/bin/docker run --rm --name pico_sign_web -p 80:80 pico_sign_web:latest
ExecStop=-/usr/bin/docker kill pico_sign_web
Restart=always
RestartSec=10s

[Install]
WantedBy=multi-user.target
```
