#!/bin/sh
set -eu
umask 077

if [ "${1:-}" = "--help" ] || [ "$#" -gt 1 ] || { [ "$#" -eq 1 ] && [ -z "$1" ]; }; then
    printf 'Usage: %s [OUTPUT_DIRECTORY]\n' "$0" >&2
    printf 'Set UFSRV_TLS_HOSTNAME to a DNS name (default: localhost).\n' >&2
    printf 'Set UFSRV_SECRET_KEY_FILE to the existing instance credential.\n' >&2
    [ "${1:-}" = "--help" ] && exit 0
    exit 2
fi

output=${1:-.}
case "$output" in
    /*|.) ;;
    *) output=./$output ;;
esac

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
credential=${UFSRV_SECRET_KEY_FILE:-${CREDENTIALS_DIRECTORY:-/opt/ufsrv/ufsrvsfu/etc/secrets}/ufsrv_secret_key}
hostname=${UFSRV_TLS_HOSTNAME:-localhost}
case "$hostname" in
    *[!a-zA-Z0-9.-]*)
        printf 'UFSRV_TLS_HOSTNAME must be a DNS name.\n' >&2
        exit 2 ;;
esac
if [ ! -r "$credential" ]; then
    printf 'Cannot read instance credential %s. Set UFSRV_SECRET_KEY_FILE.\n' "$credential" >&2
    exit 1
fi

certificate=ufsrvsfu_rest_certificate.pem
private_key=ufsrvsfu_rest_key.pem
mkdir -p -- "$output"
staging=$(mktemp -d -- "$output/.rest-tls.XXXXXX")
published_key=false
complete=false
cleanup() {
    if [ "$complete" = false ] && [ "$published_key" = true ]; then
        rm -f -- "$output/$private_key"
    fi
    rm -f -- "$staging/$certificate" "$staging/$private_key"
    rmdir -- "$staging"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -sha256 -days 365 \
    -subj "/CN=$hostname" -addext "subjectAltName=DNS:$hostname" \
    -addext 'basicConstraints=critical,CA:FALSE' -addext 'extendedKeyUsage=serverAuth' \
    -keyout "$staging/$private_key" -out "$staging/$certificate"

encrypted=$(python3 "$script_dir/../../encrypt_plaintext/encrypt_plaintext.py" \
    --credential-file "$credential" < "$staging/$private_key")
value=$(printf '%s\n' "$encrypted" | sed -n 's/^VALUE=\(enc:v1:.*\)$/\1/p')
[ -n "$value" ]

# Hard-link publication fails if the destination already exists.
ln -T -- "$staging/$private_key" "$output/$private_key"
published_key=true
ln -T -- "$staging/$certificate" "$output/$certificate"
complete=true

printf 'Certificate: %s/%s\nPrivate key: %s/%s\n' "$output" "$certificate" "$output" "$private_key" >&2
printf '%s\n' "$value"
