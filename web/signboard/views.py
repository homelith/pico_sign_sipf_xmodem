
#from django.conf import settings
from django.http import HttpResponse, JsonResponse
from django.shortcuts import render, get_object_or_404
from django.contrib.auth.decorators import login_required
from django.contrib.auth import logout as django_logout

def login(request):
    context = {}
    return render(request, 'login.html', context)


def logout(request):
    django_logout(request)
    context = {}
    return render(request, 'logout.html', context)

@login_required
def index(request):
    context = {}
    return render(request, 'index.html', context)

@login_required
def preset(request):
    context = {}
    return render(request, 'preset.html', context)

@login_required
def custom(request):
    context = {}
    return render(request, 'custom.html', context)

