import os
from pico_sign_web.settings.base import *

DEBUG = True
ALLOWED_HOSTS = ["localhost"]
DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.sqlite3",
        "NAME": os.path.join(BASE_DIR, "db.sqlite3"),
        "ATOMIC_REQUESTS": True,
    }
}
