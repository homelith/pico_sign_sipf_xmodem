FROM python:3.10 AS app
RUN pip install --upgrade pip && pip install pipenv
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates nginx xmlsec1 libxmlsec1-dev \
    && apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN mkdir /opt/app
WORKDIR /opt/app

RUN pip install gunicorn

ENV PYTHONPATH=/opt/app/
ADD Pipfile /opt/app/
ADD Pipfile.lock /opt/app/
RUN pipenv install --system

ADD manage.py /opt/app/
ADD pico_sign_web/ /opt/app/pico_sign_web/

RUN python manage.py collectstatic --settings=pico_sign_web.settings.settings_edb --noinput
RUN ln -s /opt/app/static /var/www/html/static
ADD docker/nginx.conf /etc/nginx/nginx.conf
ADD docker/default.conf /etc/nginx/sites-enabled/default

EXPOSE 80

ADD docker/entrypoint.sh /opt/app/
CMD ["bash", "-xe", "/opt/app/entrypoint.sh"]
