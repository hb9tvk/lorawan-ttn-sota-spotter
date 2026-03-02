import requests
import logging
import os
import re
import html
import json
from urllib.parse import urlparse
from urllib.parse import parse_qs
import boto3

# ----------------------------------
#
# Obtains an access_token/refresh_token from the SOTA SSO by simulating a browser-based login.
# This is actually a bit evil, but the short refresh token lifetimes (30 days) from the SOTA SSO make us do this
# for use on automated/backend systems where we don't want to require the user/owner to login again every month.
#
# Returns an dict with the response from the SSO token endpoint, including 'access_token' and 'refresh_token' keys.
#
def sotaSsoLogin(username, password, clientId="sotawatch", redirectUri="https://sotawatch.sota.org.uk/"):
    codeChallenge=os.urandom(32).hex()
    loginParams= {
        'client_id': clientId,
        'redirect_uri': redirectUri,
        'response_mode': 'fragment',
        'response_type': 'code',
        'scope': 'openid',
        'code_challenge': codeChallenge,
        'code_challenge_method': 'plain'
    }
    session=requests.Session()
    loginUrl="https://sso.sota.org.uk/auth/realms/SOTA/protocol/openid-connect/auth"
    # Load login page (HTML)
    loginPage=session.get(loginUrl, params=loginParams)
    # Extract form action
    match=re.search('action="(https.*?)"',loginPage.content.decode('utf-8'))
    if match==None:
        raise RuntimeError("Could not find action in login form")
    action=html.unescape(match.group(1))
    # POST credentials
    authParams={
        'username': username,
        'password': password
    }
    result=session.post(action, data=authParams)
    # if we get a redirect (302), the login was successful
    if len(result.history)==0:
        raise RuntimeError("Did not get redirect. Wrong user/passwd?")

    location=result.history[0].headers["location"]
    parsedUrl=urlparse(location)
    authorizationCode=parse_qs(parsedUrl.fragment)['code'][0]
    # Redeem authorization code to get access/refresh tokens
    tokenParams= {
        'code': authorizationCode,
        'grant_type': 'authorization_code',
        'client_id': clientId,
        'redirect_uri': redirectUri,
        'code_verifier': codeChallenge
    }
    tokenUrl="https://sso.sota.org.uk/auth/realms/SOTA/protocol/openid-connect/token"
    result=session.post(tokenUrl, data=tokenParams)
    token=json.loads(result.content)
    return(token)

# Obtain a new access token using only the refresh token 
# (obtained as the 'refresh_token' key from a sotaSsoLogin call).
def getNewAccessToken(refreshToken, clientId='sotawatch'):
    session=requests.Session()
    tokenParams= {
        'refresh_token': refreshToken,
        'grant_type': 'refresh_token',
        'client_id': clientId
    }
    tokenUrl="https://sso.sota.org.uk/auth/realms/SOTA/protocol/openid-connect/token"
    result=session.post(tokenUrl, data=tokenParams)
    token=json.loads(result.content)
    return(token)

logger = logging.getLogger()
logger.setLevel(logging.INFO)

def lambda_handler(event, context):
    s3=boto3.resource('s3')
    obj=s3.Object(os.environ['S3_BUCKET'], 'refresh.txt')
    refreshToken=obj.get()['Body'].read().decode('utf-8').strip()
    logger.info(refreshToken)
    t=getNewAccessToken(refreshToken)
    logger.info(t)
    logger.info("Storing new refresh token")
    obj.put(Body=t["refresh_token"])
    return(t)
