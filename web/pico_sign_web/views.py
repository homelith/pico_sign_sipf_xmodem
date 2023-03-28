from django.conf import settings
from django.http import HttpResponse, JsonResponse
from django.shortcuts import render, get_object_or_404
from django.contrib.auth.decorators import login_required
from django.contrib.auth import logout as django_logout

import requests
from requests.auth import HTTPBasicAuth

import numpy
from PIL import Image

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

def upload(request, context, png_name, scroll_speed):
    ############################################################################
    # convert PNG to rgb565 BMP
    ############################################################################
    image = Image.open('pico_sign_web/static/' + png_name).transpose(Image.ROTATE_270)
    width, height = image.size
    if image.mode == 'P':
        palette = np.array(image.getpalette()).reshape(-1, 3)
        getPixel = lambda x,y: palette[image.getpixel((x, y))]
    else:
        getPixel = lambda x,y: image.getpixel((x, y))

    bin_name = png_name.rsplit('.', 1)[0] + '.bin'
    bin_fp = open('pico_sign_web/static/' + bin_name, 'wb')
    for y in range(height):
        for x in range(width):
            rgb888 = getPixel(x, y)
            rgb565 = ((rgb888[0]>>3)<<11) | ((rgb888[1]>>2)<<5) | (rgb888[2]>>3)
            bin_fp.write(rgb565.to_bytes(2, byteorder = "little"))
    bin_fp.close()

    ############################################################################
    # uploading image bin to mono-PF
    # refers : https://manual.sakura.ad.jp/cloud/iotpf/api.html#iotpf-api-filetransfer
    ############################################################################
    # create common service item id on sakura cloud
    ret = requests.put( \
        settings.SAKURACLOUD_COMMONSERVICEITEM_ENDPOINT + '/' + settings.SIPF_PROJECT_ID + '/iotplatform/vmgw/files/' + bin_name + '/stat', \
        auth=HTTPBasicAuth(settings.SAKURACLOUD_ACCESS_TOKEN, settings.SAKURACLOUD_ACCESS_TOKEN_SECRET), \
    )
    if not (ret.status_code == 200 or ret.status_code == 201):
        return render(request, 'error.html', context)

    # file upload request
    ret = requests.put( \
        settings.SAKURACLOUD_COMMONSERVICEITEM_ENDPOINT + '/' + settings.SIPF_PROJECT_ID + '/iotplatform/vmgw/files/' + bin_name + '/upload_request', \
        auth=HTTPBasicAuth(settings.SAKURACLOUD_ACCESS_TOKEN, settings.SAKURACLOUD_ACCESS_TOKEN_SECRET), \
    )
    if ret.status_code != 200:
        return render(request, 'error.html', context)
    ret_json = ret.json()
    upload_url = ret_json['IoTPlatform']['url']
    upload_token = ret_json['IoTPlatform']['token']

    # file upload
    bin_fp = open('pico_sign_web/static/' + bin_name, 'rb')
    headers_dict = {
        'Authorization' : 'Bearer ' + upload_token,
    }
    ret = requests.put( \
        upload_url, \
        headers=headers_dict, \
        files={'attachment': bin_fp}, \
    )
    bin_fp.close()
    if ret.status_code != 202:
        return render(request, 'error.html', context)

    ############################################################################
    # submit bin_name message to mono-PF
    # refers : https://manual.sakura.ad.jp/cloud/iotpf/service-adapter/service-adapter-incoming-webhook.html
    ############################################################################
    headers_dict = {'Content-Type' : 'application/json'}
    json_dict = {
        'device_id' : settings.SIPF_DEVICE_ID,
        'type'      : 'object',
        'payload'   : [
            {
                'type'  : 'string.utf8',
                'tag'   : 'FF',
                'value' : bin_name,
            }
        ],
    }
    result = requests.post(settings.SIPF_WEBHOOK_ENDPOINT, headers=headers_dict, json=json_dict)

    ############################################################################
    # submit scroll_speed message to mono-PF
    ############################################################################
    headers_dict = {'Content-Type' : 'application/json'}
    json_dict = {
        'device_id' : settings.SIPF_DEVICE_ID,
        'type'      : 'object',
        'payload'   : [
            {
                'type'  : 'int16',
                'tag'   : 'FF',
                'value' : scroll_speed,
            }
        ],
    }
    result = requests.post(settings.SIPF_WEBHOOK_ENDPOINT, headers=headers_dict, json=json_dict)

    # inject result page parameters
    context["bin_name"] = bin_name
    context["scroll_speed"] = scroll_speed
    return render(request, 'done.html', context)


@login_required
def preset(request):
    context = {}

    # POST only
    if request.method != 'POST':
        return render(request, 'error.html', context)

    # check if valid POST parameter exists
    param_str = request.POST.get('param_str', None)
    if param_str == None:
        return render(request, 'error.html', context)

    # parse "{filename}:{scroll}" string into parameters
    params = param_str.split(':')
    if len(params) != 2:
        return render(request, 'error.html', context)
    if not params[1].isdigit():
        return render(request, 'error.html', context)
    png_name = params[0]
    scroll_speed = int(params[1])
    if scroll_speed > 32767:
        scroll_speed = 32767
    if scroll_speed < -32768:
        scroll_speed = -32768

    return upload(request, context, png_name, scroll_speed)

@login_required
def custom(request):
    context = {}

    # POST only
    if request.method != 'POST':
        return render(request, 'error.html', context)

    # check if valid POST multipart upload + scroll_speed number exists
    png_body = request.FILES.get('png_body')
    if png_body == None:
        return render(request, 'error.html', context)
    scroll_speed_str = request.POST.get('scroll_speed')
    if scroll_speed_str == None:
        return render(request, 'error.html', context)
    if not scroll_speed_str.isdigit():
        return render(request, 'error.html', context)

    # store multipart upload
    png_name = 'custom.png'
    png_fp = open('pico_sign_web/static/' + png_name, 'wb')
    for chunk in png_body.chunks():
        png_fp.write(chunk)
    png_fp.close()

    # store scroll_speed number
    scroll_speed = int(scroll_speed_str)
    if scroll_speed > 32767:
        scroll_speed = 32767
    if scroll_speed < -32768:
        scroll_speed = -32768

    return upload(request, context, png_name, scroll_speed)

