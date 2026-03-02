import json
import logging
import requests
import boto3
import os
import re
import html
import time
import struct
import base64
from urllib.parse import urlparse, parse_qs

logger = logging.getLogger()
logger.setLevel(logging.INFO)

S3_BUCKET      = 'hb9tvk-sotaspot'
S3_REFRESH_KEY = 'refresh.txt'
SPOT_URL       = 'https://api-db2.sota.org.uk/api/spots'
SSO_BASE       = 'https://sso.sota.org.uk/auth/realms/SOTA/protocol/openid-connect'
MODES          = ["CW", "SSB", "FM", "DATA"]
MESSAGES       = ["QRV now", "QRT", "TEST"]

# Module-level cache — survives Lambda warm starts, avoiding a login round-trip
# on every invocation. Access tokens from the SOTA SSO are valid for ~5 minutes.
_access_token      = None
_token_expires_at  = 0.0


def _sso_login(username, password, client_id="sotawatch",
               redirect_uri="https://sotawatch.sota.org.uk/"):
    """
    Full browser-simulated PKCE login flow.
    Required because the SOTA SSO (Keycloak) does not expose the
    OAuth2 password grant for this client.
    Returns the full token dict (access_token, refresh_token, expires_in, …).
    """
    code_challenge = os.urandom(32).hex()
    session = requests.Session()
    login_page = session.get(
        f"{SSO_BASE}/auth",
        params={
            'client_id':             client_id,
            'redirect_uri':          redirect_uri,
            'response_mode':         'fragment',
            'response_type':         'code',
            'scope':                 'openid',
            'code_challenge':        code_challenge,
            'code_challenge_method': 'plain',
        }
    )
    match = re.search(r'action="(https.*?)"', login_page.content.decode('utf-8'))
    if match is None:
        raise RuntimeError("Could not find form action in SSO login page")
    action = html.unescape(match.group(1))

    result = session.post(action, data={'username': username, 'password': password})
    if not result.history:
        raise RuntimeError("SSO login failed — wrong username or password?")

    location = result.history[0].headers["location"]
    auth_code = parse_qs(urlparse(location).fragment)['code'][0]

    token_resp = session.post(
        f"{SSO_BASE}/token",
        data={
            'code':          auth_code,
            'grant_type':    'authorization_code',
            'client_id':     client_id,
            'redirect_uri':  redirect_uri,
            'code_verifier': code_challenge,
        }
    )
    return token_resp.json()


def _refresh_token(refresh_tok, client_id='sotawatch'):
    """Exchange a refresh token for a new token dict."""
    resp = requests.post(
        f"{SSO_BASE}/token",
        data={
            'refresh_token': refresh_tok,
            'grant_type':    'refresh_token',
            'client_id':     client_id,
        }
    )
    return resp.json()


def _get_access_token():
    """
    Returns a valid SOTA SSO access token using a three-level strategy:

    1. Cached access token (module-level, warm Lambda reuse).
    2. Refresh token stored in S3 — cheap, avoids a full HTML login.
    3. Full username/password login using SOTA_USERNAME / SOTA_PASSWORD
       Lambda environment variables — runs when there is no valid refresh
       token (first deploy, or after the 30-day refresh token expiry).

    After a successful refresh or login, the new refresh token is written
    back to S3 so the next invocation can use strategy 2 again.
    """
    global _access_token, _token_expires_at

    # 1. Use cached token if still valid (30-second safety buffer)
    if _access_token and time.time() < _token_expires_at - 30:
        logger.info("Auth: using cached access token")
        return _access_token

    s3_obj = boto3.resource('s3').Object(S3_BUCKET, S3_REFRESH_KEY)
    token = None

    # 2. Try refresh token from S3
    try:
        refresh_tok = s3_obj.get()['Body'].read().decode('utf-8').strip()
        token = _refresh_token(refresh_tok)
        if 'access_token' not in token:
            logger.warning("Auth: S3 refresh token rejected (%s) — falling back to login",
                           token.get('error', '?'))
            token = None
        else:
            logger.info("Auth: got access token via S3 refresh token")
    except Exception as exc:
        logger.warning("Auth: could not use S3 refresh token (%s) — falling back to login", exc)

    # 3. Full login with credentials from environment variables
    if token is None:
        logger.info("Auth: performing full SSO login")
        token = _sso_login(os.environ['SOTA_USERNAME'], os.environ['SOTA_PASSWORD'])
        if 'access_token' not in token:
            raise RuntimeError(f"SSO login failed: {token}")
        logger.info("Auth: got access token via full SSO login")

    # Persist new refresh token to S3 so future invocations use strategy 2
    if 'refresh_token' in token:
        s3_obj.put(Body=token['refresh_token'])
        logger.info("Auth: stored new refresh token in S3")

    # Cache access token for the lifetime of this Lambda container
    _access_token     = token['access_token']
    _token_expires_at = time.time() + token.get('expires_in', 300)
    return _access_token


def lambda_handler(event, context):
    body = json.loads(event['body'])

    # Decode the packed binary struct directly from the base64 frm_payload.
    # Layout (16 bytes, little-endian, __attribute__((packed))):
    #   bytes  0–10 : ref[11]  — null-padded summit reference string
    #   bytes 11–14 : qrg      — frequency in kHz as 32-bit float
    #   byte  15    : modmsg   — low nibble = mode index, high nibble = msg index
    raw = base64.b64decode(body['uplink_message']['frm_payload'])
    ref_bytes, qrg_raw, modmsg = struct.unpack_from('<11sfB', raw)
    ref      = ref_bytes.decode('ascii').rstrip('\x00')
    mode_idx = modmsg & 0x0F
    msg_idx  = (modmsg >> 4) & 0x0F
    logger.info("Decoded payload: ref=%s qrg=%.1f mode=%d msg=%d", ref, qrg_raw, mode_idx, msg_idx)

    if mode_idx >= len(MODES) or msg_idx >= len(MESSAGES):
        raise ValueError(
            f"Payload index out of range: mode={mode_idx}, msg={msg_idx} — "
            "struct layout mismatch between firmware and lambda"
        )

    callsign = os.environ.get('SOTA_CALLSIGN', 'HB9TVK')
    qrg      = qrg_raw / 1000.0   # kHz → MHz
    is_test  = MESSAGES[msg_idx] == "TEST"

    spot_param = {
        "callsign":          callsign,
        "activatorCallsign": callsign + "/P",
        "associationCode":   ref.split('/')[0],
        "summitCode":        ref.split('/')[1],
        "frequency":         str(qrg),
        "mode":              MODES[mode_idx],
        "type":              "TEST" if is_test else "NORMAL",
        "comments":          "Test - Ignore" if is_test else MESSAGES[msg_idx],
    }
    if is_test:
        logger.info("Test mode — spot will be posted as type TEST")
    logger.info("Posting spot: %s", spot_param)

    access_token = _get_access_token()
    r = requests.post(
        SPOT_URL,
        json=spot_param,
        headers={
            "Authorization": "Bearer " + access_token,
            "Content-Type":  "application/json",
        }
    )
    logger.info("SOTA API response: %d %s", r.status_code, r.text)
    r.raise_for_status()   # non-2xx → exception → Lambda 500 → TTN retries

    return {'statusCode': 200, 'body': 'ok'}
