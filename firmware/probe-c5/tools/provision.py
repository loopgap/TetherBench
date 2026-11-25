"""Generate ignored device credentials using installed IDF Python cryptography."""
import argparse
import datetime
import json
import pathlib
import secrets
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCAL = ROOT / ".local"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--wifi", type=pathlib.Path, default=LOCAL / "wifi.json")
    parser.add_argument("--ap", action="store_true", help="Use this C5 as the Wi-Fi 6 AP")
    args = parser.parse_args()
    LOCAL.mkdir(exist_ok=True)
    wifi = json.loads(args.wifi.read_text(encoding="utf-8-sig"))
    ssid, password = wifi["ssid"], wifi["password"]
    if not 1 <= len(ssid.encode()) <= 32 or not 8 <= len(password.encode()) <= 63:
        raise SystemExit("Wi-Fi credentials are outside WPA2/WPA3 limits.")
    for name in ("server", "update"):
        path = LOCAL / (name + ".key")
        if not path.exists():
            key = ec.generate_private_key(ec.SECP256R1())
            path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    key = serialization.load_pem_private_key((LOCAL / "server.key").read_bytes(), None)
    if not (LOCAL / "server.crt").exists():
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "TetherBench C5")])
        now = datetime.datetime.now(datetime.timezone.utc)
        cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(days=1))
            .not_valid_after(now + datetime.timedelta(days=3650))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
        (LOCAL / "server.crt").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    cert = x509.load_pem_x509_certificate((LOCAL / "server.crt").read_bytes())
    device_file = LOCAL / "device.json"
    device = json.loads(device_file.read_text()) if device_file.exists() else {"token": secrets.token_hex(32)}
    device["certificate_sha256"] = cert.fingerprint(hashes.SHA256()).hex()
    device["board"] = "esp32c5-n4"
    device_file.write_text(json.dumps(device, indent=2) + "\n")
    signer = serialization.load_pem_private_key((LOCAL / "update.key").read_bytes(), None)
    public = signer.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    # The board mask excludes flash, USB, UART0 and conservative boot restrictions.
    allowed = sum(1 << p for p in (0, 1, 2, 3, 4, 5, 6, 10, 15, 23))
    header = ['#pragma once', '#include <stdint.h>']
    for name, value in (("TB_WIFI_SSID", ssid.encode()), ("TB_WIFI_PASSWORD", password.encode()),
                        ("TB_AUTH_TOKEN", device["token"].encode()), ("TB_UPDATE_PUBLIC_KEY", public),
                        ("TB_CERT_SHA256", cert.fingerprint(hashes.SHA256()))):
        header.append('static const uint8_t ' + name + '[] = {' + ','.join(str(b) for b in value) + ',0};')
    header += ['#define TB_WIFI_AP ' + str(int(args.ap)), '#define TB_ALLOWED_GPIO_MASK ' + str(allowed) + 'u']
    (LOCAL / "device_config.h").write_text("\n".join(header) + "\n")
    print("Provisioned local credentials; certificate SHA-256 " + device["certificate_sha256"])

if __name__ == "__main__":
    main()
